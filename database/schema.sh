#!/bin/bash
mysql -u root -p"${MYSQL_ROOT_PASSWORD}" <<-EOSQL
    CREATE DATABASE IF NOT EXISTS \`${DB_NAME}\`;
    USE \`${DB_NAME}\`;

    CREATE USER IF NOT EXISTS '${DB_USER}'@'%' IDENTIFIED BY '${DB_PASS}';
    GRANT ALL PRIVILEGES ON \`${DB_NAME}\`.* TO '${DB_USER}'@'%';
    FLUSH PRIVILEGES;

    CREATE TABLE IF NOT EXISTS \`mailboxData\` (
        \`id\` INT NOT NULL AUTO_INCREMENT,
        \`datetime\` DATETIME NOT NULL,
        \`deviceID\` INT DEFAULT NULL,
        \`triggerEvent\` INT DEFAULT NULL,
        \`temp\` DECIMAL(5, 2) DEFAULT NULL,
        \`rssi\` INT DEFAULT NULL,
        PRIMARY KEY (\`id\`),
        INDEX \`idx_mail_time\` (\`datetime\`)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
EOSQL