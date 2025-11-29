CREATE TABLE IF NOT EXISTS `customs`.`gv_teleport_back` (
  `player`     INT UNSIGNED NOT NULL,
  `map`        INT UNSIGNED NOT NULL,
  `positionx`  DOUBLE NOT NULL,
  `positiony`  DOUBLE NOT NULL,
  `positionz`  DOUBLE NOT NULL,
  `orientation` FLOAT NOT NULL,
  `set_time`   TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`player`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
