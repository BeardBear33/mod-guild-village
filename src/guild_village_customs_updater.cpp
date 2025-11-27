// Minimalistický autoupdater pro CUSTOMS SQL (Guild Village)
// - hledá .sql v data/sql/customs/{base,updates_include,updates}
// - aplikuje po jednom SQL příkazu (parser ignoruje komentáře, USE, DELIMITER)
// - trackuje v customs.gv_updates
// - early bootstrap: vytvoří schéma `customs` + základní tabulky, aby první start nepadal

#include "ScriptMgr.h"
#include "DatabaseEnv.h"
#include "Config.h"
#include "Log.h"
#include "CryptoHash.h"   // Acore::Crypto::SHA1
#include "Util.h"         // StringEqualI

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;
using Acore::Crypto::SHA1;

// ---------- helpers ----------
static inline std::string Trim(std::string s)
{
    auto notspace = [](int ch){ return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
    return s;
}

// Odstraní SQL komentáře: --, # a /* ... */ mimo stringy
static std::string StripSqlComments(std::string const& in)
{
    std::string out; out.reserve(in.size());
    bool inS=false, inD=false, inB=false; // ' " `
    for (size_t i=0; i<in.size(); )
    {
        char c=in[i], n = (i+1<in.size()? in[i+1] : '\0');

        // přepínej stringy
        if (!inD && !inB && c=='\''){ inS=!inS; out.push_back(c); ++i; continue; }
        if (!inS && !inB && c=='"'){  inD=!inD; out.push_back(c); ++i; continue; }
        if (!inS && !inD && c=='`'){  inB=!inB; out.push_back(c); ++i; continue; }

        if (!inS && !inD && !inB)
        {
            // --
            if (c=='-' && n=='-'){ i+=2; while (i<in.size() && in[i]!='\n') ++i; continue; }
            // #
            if (c=='#'){ ++i; while (i<in.size() && in[i]!='\n') ++i; continue; }
            // /* ... */
            if (c=='/' && n=='*'){
                i+=2;
                while (i+1<in.size() && !(in[i]=='*' && in[i+1]=='/')) ++i;
                if (i+1<in.size()) i+=2;
                continue;
            }
        }

        out.push_back(c); ++i;
    }
    return out;
}

// Rozseká na příkazy dle ';' mimo stringy. Zahazuje USE... a DELIMITER...
static std::vector<std::string> SplitSqlStatements(std::string const& src)
{
    std::vector<std::string> out;
    std::string cur; cur.reserve(src.size());
    bool inS=false, inD=false, inB=false;

    auto isIgnore = [](std::string s){
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch){ return std::toupper(ch); });
        return s.rfind("USE ",0)==0 || s.rfind("DELIMITER ",0)==0 || s=="SELECT 1";
    };

    for (size_t i=0;i<src.size();++i)
    {
        char c=src[i];
        if (c=='\'' && !inD && !inB){ inS=!inS; cur.push_back(c); continue; }
        if (c=='"'  && !inS && !inB){ inD=!inD; cur.push_back(c); continue; }
        if (c=='`'  && !inS && !inD){ inB=!inB; cur.push_back(c); continue; }

        if (c==';' && !inS && !inD && !inB)
        {
            std::string stmt = Trim(cur); cur.clear();
            if (!stmt.empty() && !isIgnore(stmt)) out.emplace_back(std::move(stmt));
            continue;
        }
        cur.push_back(c);
    }
    std::string tail = Trim(cur);
    if (!tail.empty())
    {
        auto up = tail; std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch){ return std::toupper(ch); });
        if (!(up.rfind("USE ",0)==0 || up.rfind("DELIMITER ",0)==0 || up=="SELECT 1"))
            out.emplace_back(std::move(tail));
    }
    return out;
}

static std::string ReadFile(std::string const& path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return {};
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

static std::string Sha1Hex(std::string const& data)
{
    auto const d = SHA1::GetDigestOf(data);
    static char const* hex="0123456789abcdef";
    std::string out; out.reserve(d.size()*2);
    for (uint8_t b : d){ out.push_back(hex[(b>>4)&0xF]); out.push_back(hex[b&0xF]); }
    return out;
}

// ---------- tracking tabulka ----------
static void EnsureTrackingTable()
{
    WorldDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `customs`.`gv_updates` ("
        "  `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
        "  `filename` VARCHAR(255) NOT NULL,"
        "  `sha1` CHAR(40) NOT NULL DEFAULT '',"
        "  `applied_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (`id`),"
        "  UNIQUE KEY `uq_filename` (`filename`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"
    );
}

static std::unordered_set<std::string> LoadApplied()
{
    std::unordered_set<std::string> seen;
    if (QueryResult r = WorldDatabase.Query("SELECT `filename` FROM `customs`.`gv_updates`"))
        do { seen.insert(r->Fetch()[0].Get<std::string>()); } while (r->NextRow());
    return seen;
}

static void MarkApplied(std::string const& filename, std::string const& sha1)
{
    WorldDatabase.DirectExecute(
        "INSERT IGNORE INTO `customs`.`gv_updates` (`filename`,`sha1`) VALUES ('{}','{}')",
        filename, sha1
    );
}

// ---------- early bootstrap ----------
static void EnsureBootstrapEarly()
{
    // 1) Schéma `customs`
    WorldDatabase.DirectExecute("CREATE DATABASE IF NOT EXISTS `customs` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");

    // 2) Minimal set tabulek, které může core očekávat hned na startu
    WorldDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `customs`.`gv_loot` ("
        "  `entry` INT UNSIGNED NOT NULL,"
        "  `currency` ENUM('material1','material2','material3','material4','random') NOT NULL,"
        "  `chance` FLOAT NOT NULL DEFAULT 100,"
        "  `min_amount` INT UNSIGNED NOT NULL DEFAULT 1,"
        "  `max_amount` INT UNSIGNED NOT NULL DEFAULT 1,"
        "  `comment` VARCHAR(255) NULL,"
        "  PRIMARY KEY (`entry`,`currency`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci"
    );

    // tracking tabulka
    EnsureTrackingTable();
}

// ---------- file collect ----------
static void CollectSqlFiles(std::string const& root, std::vector<std::string>& out)
{
    std::error_code ec;
    if (!fs::exists(root, ec))
    {
        LOG_INFO("gv.customs", "[customs] Dir not found: {}", root);
        return;
    }
    for (auto const& de : fs::directory_iterator(root, ec))
    {
        if (ec) break;
        if (!de.is_regular_file()) continue;
        auto p = de.path();
        if (p.has_extension() && ::StringEqualI(p.extension().string(), ".sql"))
            out.emplace_back(p.string());
    }
    std::sort(out.begin(), out.end());
}

// ---------- executor ----------
static bool ExecuteSqlFile(std::string const& filePath, std::string const& keyForDb)
{
    std::string raw = ReadFile(filePath);
    std::string sha = Sha1Hex(raw);

    if (raw.empty())
    {
        LOG_WARN("gv.customs", "[customs] Empty file -> mark applied: {} (sha1={})", keyForDb, sha);
        MarkApplied(keyForDb, sha);
        return true;
    }

    // Odstripovat BOM, pokud je
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        raw.erase(0,3);

    std::string cleaned = StripSqlComments(raw);
    auto stmts = SplitSqlStatements(cleaned);

    if (stmts.empty())
    {
        LOG_INFO("gv.customs", "[customs] {}: nothing to execute after stripping comments (mark applied).", keyForDb);
        MarkApplied(keyForDb, sha);
        return true;
    }

    LOG_INFO("gv.customs", "[customs] Executing {} statement(s) from {}", stmts.size(), keyForDb);
    for (auto const& s : stmts)
    {
        std::string prev = s.substr(0, std::min<size_t>(160, s.size()));
        WorldDatabase.DirectExecute(s);
        LOG_DEBUG("gv.customs", "[customs] exec: {}{}", prev.c_str(), s.size()>160?" ...":"");
    }

    MarkApplied(keyForDb, sha);
    LOG_INFO("gv.customs", "[customs] Applied: {}", keyForDb);
    return true;
}

static std::string RelKey(std::string const& root, std::string const& full)
{
    std::error_code ec;
    auto rel = fs::relative(full, root, ec);
    if (!ec) return (fs::path(root).filename() / rel).string();
    return fs::path(full).filename().string();
}

static void RunPass(char const* label, fs::path const& dir, std::unordered_set<std::string>& applied, uint32& nApplied)
{
    std::vector<std::string> files; CollectSqlFiles(dir.string(), files);
    for (auto const& full : files)
    {
        std::string key = (std::string(label) + "/" + RelKey(dir.string(), full));
        if (applied.count(key)) continue;
        if (ExecuteSqlFile(full, key)){ applied.insert(key); ++nApplied; }
    }
}

// ---------- module root ----------
static fs::path ModuleRoot()
{
    fs::path p(__FILE__);
    return p.parent_path().parent_path(); // .../modules/mod-guild-village
}

// ---------- WorldScript ----------
class GV_Customs_UpdaterWS : public WorldScript
{
public:
    GV_Customs_UpdaterWS()
        : WorldScript("GV_Customs_UpdaterWS", std::vector<uint16>{ WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) {}

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        LOG_INFO("gv.customs", "[customs] Early bootstrap (schema + minimal tables)...");
        EnsureBootstrapEarly();
    }

    void OnStartup() override
    {
        fs::path root    = ModuleRoot();
        fs::path sqlRoot = root / "data/sql/customs";
        fs::path dirBase = sqlRoot / "base";
        fs::path dirInc  = sqlRoot / "updates_include";
        fs::path dirUpd  = sqlRoot / "updates";

        LOG_INFO("gv.customs", "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓");
        LOG_INFO("gv.customs", "┃    Guild Village – Customs SQL Updater (auto)      ┃");
        LOG_INFO("gv.customs", "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛");
        LOG_INFO("gv.customs", "[customs] SQL root: {}", sqlRoot.string());

        EnsureTrackingTable();

        auto applied    = LoadApplied();
        uint32 appliedN = 0;

        RunPass("base",    dirBase, applied, appliedN);
        RunPass("include", dirInc,  applied, appliedN);
        RunPass("updates", dirUpd,  applied, appliedN);

        if (appliedN==0) LOG_INFO("gv.customs","[customs] Nothing to update – up to date.");
        else             LOG_WARN("gv.customs","[customs] Applied {} file(s). If schema/gameplay changed, restart is recommended.", appliedN);
    }
};

void RegisterGuildVillageCustomsUpdater(){ new GV_Customs_UpdaterWS(); }
