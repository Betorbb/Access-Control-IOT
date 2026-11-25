require('dotenv').config();
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const mysql = require('mysql2/promise');
const mqtt = require('mqtt');
const cors = require('cors');
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server, { cors: { origin: '*' } });

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

const PORT = process.env.PORT || 3001;
const DEVICE_ID = 'ESP32-AC-001';

const pool = mysql.createPool({
  host: process.env.DB_HOST || '127.0.0.1',
  user: process.env.DB_USER || 'access_user',
  password: process.env.DB_PASS || 'access_pass',
  database: process.env.DB_NAME || 'access_control',
  waitForConnections: true,
  connectionLimit: 10
});

const MQTT_BROKER = process.env.MQTT_URL || 'mqtt://broker.hivemq.com:1883';
const mqttClient = mqtt.connect(MQTT_BROKER);

mqttClient.on('connect', () => {
  console.log('Conectado ao Broker MQTT:', MQTT_BROKER);
  mqttClient.subscribe(`access-control/${DEVICE_ID}/telemetry`);
  mqttClient.subscribe(`access-control/${DEVICE_ID}/events`);
  mqttClient.subscribe(`access-control/${DEVICE_ID}/status`);
});

mqttClient.on('message', async (topic, message) => {
  try {
    const data = JSON.parse(message.toString());

    if (topic.endsWith('/telemetry')) {
      await pool.query(
        'INSERT INTO telemetry (device_id, temperature, humidity, battery, signal_strength, wifi_rssi, uptime) VALUES (?, ?, ?, ?, ?, ?, ?)',
        [
          data.device_id || DEVICE_ID,
          data.temperature ?? 0,
          data.humidity ?? 0,
          data.battery ?? 0,
          data.signal_strength ?? 0,
          data.wifi_rssi ?? 0,
          data.uptime ?? 0
        ]
      );
      io.emit('telemetry_update', data);
    } else if (topic.endsWith('/events')) {
      const details = data.details || (data.rfid_uid ? `RFID UID: ${data.rfid_uid}` : 'Evento recebido');
      await pool.query(
        'INSERT INTO events (device_id, event_type, details) VALUES (?, ?, ?)',
        [data.device_id || DEVICE_ID, data.event_type || 'event', details]
      );
      io.emit('event_update', { ...data, details, created_at: new Date() });
    } else if (topic.endsWith('/status')) {
      io.emit('status_update', data);
    }
  } catch (err) {
    console.error('Erro ao processar mensagem MQTT:', err.message);
  }
});

app.post('/api/access', async (req, res) => {
  const uid = String(req.body.uid || '').toUpperCase().trim();
  if (!uid) {
    return res.status(400).json({ granted: false, error: 'UID obrigatório' });
  }

  try {
    const [rows] = await pool.query(
      'SELECT id, uid, user_name, active FROM authorized_cards WHERE uid = ? AND active = TRUE LIMIT 1',
      [uid]
    );

    if (rows.length > 0) {
      return res.json({
        granted: true,
        uid: rows[0].uid,
        user: rows[0].user_name
      });
    }

    return res.json({
      granted: false,
      uid,
      user: null
    });
  } catch (err) {
    console.error('Erro em POST /api/access:', err);
    return res.status(500).json({ granted: false, error: 'Erro interno no banco' });
  }
});

app.get('/api/dashboard', async (req, res) => {
  try {
    const [telemetry] = await pool.query('SELECT * FROM telemetry ORDER BY id DESC LIMIT 1');
    const [events] = await pool.query('SELECT * FROM events ORDER BY id DESC LIMIT 15');
    const [cards] = await pool.query('SELECT * FROM authorized_cards ORDER BY id DESC');
    res.json({
      latestTelemetry: telemetry[0] || null,
      events,
      cards
    });
  } catch (err) {
    res.status(500).json({ error: err.message });
  }
});

app.post('/api/cards', async (req, res) => {
  console.log('Recebido POST /api/cards:', req.body);
  const uid = String(req.body.uid || '').toUpperCase().trim();
  const user_name = String(req.body.user_name || '').trim();

  if (!uid || !user_name) {
    return res.status(400).json({ error: 'Preencha UID e Nome' });
  }

  try {
    await pool.query(
      'INSERT INTO authorized_cards (uid, user_name, active) VALUES (?, ?, TRUE)',
      [uid, user_name]
    );
    console.log('Cartão cadastrado com sucesso:', uid, user_name);
    return res.status(201).json({ success: true, uid, user_name });
  } catch (err) {
    console.error('Erro ao inserir no MySQL:', err);
    if (err.code === 'ER_DUP_ENTRY') {
      return res.status(409).json({ error: 'UID já cadastrado' });
    }
    return res.status(500).json({ error: err.sqlMessage || err.message });
  }
});

app.post('/api/commands', (req, res) => {
  const { command, payload } = req.body;
  if (!command) return res.status(400).json({ error: 'Comando não informado' });

  const topic = `access-control/${DEVICE_ID}/commands`;
  const message = JSON.stringify({ command, payload: payload || {} });

  mqttClient.publish(topic, message, (err) => {
    if (err) return res.status(500).json({ error: 'Falha ao publicar MQTT' });
    console.log(`Comando enviado [${topic}]: ${message}`);
    res.json({ success: true, sent: { command, payload } });
  });
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`Servidor rodando em http://localhost:${PORT}`);
});