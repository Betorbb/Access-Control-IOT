
CREATE DATABASE IF NOT EXISTS access_control;
USE access_control;

CREATE TABLE IF NOT EXISTS authorized_cards (
    id INT AUTO_INCREMENT PRIMARY KEY,
    uid VARCHAR(32) NOT NULL UNIQUE,
    user_name VARCHAR(100) NOT NULL,
    active BOOLEAN DEFAULT TRUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT IGNORE INTO authorized_cards (uid, user_name, active)
VALUES ('01020304', 'Roberto Bueno', TRUE);

CREATE TABLE IF NOT EXISTS telemetry (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    temperature FLOAT,
    humidity FLOAT,
    battery INT,
    signal_strength INT,
    wifi_rssi INT,
    uptime INT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS events (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(50) NOT NULL,
    event_type VARCHAR(50) NOT NULL,
    details VARCHAR(255),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
