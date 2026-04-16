UPDATE `customs`.`gv_expansion_gameobjects`
SET `rotation3` = 0
WHERE `rotation3` = 1;

UPDATE `customs`.`gv_gameobject_template`
SET `rotation3` = 0
WHERE `rotation3` = 1;