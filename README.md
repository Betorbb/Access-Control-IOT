# Sistema de Controle de Acesso IoT (ESP32 + RFID RC522 + MQTT + Node.js + MySQL)

Sistema completo de controle de acesso inteligente desenvolvido com **ESP32** (simulado no **Wokwi**), leitor **RFID MFRC522**, sensores **PIR** e **DHT22**, atuadores físicos (**relé**, **LEDs** e **buzzer**), comunicação híbrida via **HTTP/HTTPS** e **MQTT** (`broker.hivemq.com`), servidor backend em **Node.js**, banco de dados relacional **MySQL** e interface web de monitoramento e controle em tempo real.

---

## Arquitetura e Fluxo de Funcionamento

1. **Leitura de Tag RFID (`access-control/sketch.ino`):** ao aproximar um cartão/tag do leitor MFRC522, o ESP32 captura o UID em formato hexadecimal maiúsculo e publica o evento `rfid_detected` via MQTT.
2. **Validação de Acesso (`POST /api/access`):** o microcontrolador envia uma requisição HTTP POST contendo o UID para o servidor Node.js (exposto durante testes locais via **Cloudflare Tunnel**).
3. **Persistência e Regra de Negócio (`server.js` + `database.sql`):** o backend consulta a tabela `authorized_cards` no MySQL, verifica se o cartão está cadastrado e ativo, grava o evento no histórico e responde com `granted` (`true` ou `false`) e o nome do usuário.
4. **Atuação e Sinalização no Hardware:**
   - **Acesso Liberado:** aciona o relé (`RELAY_PIN`) por 5 segundos, pisca o LED verde e emite sinal sonoro de confirmação no buzzer.
   - **Acesso Negado:** mantém a porta trancada, pisca o LED vermelho 3 vezes junto ao buzzer e registra a tentativa negada.
5. **Telemetria, Sensor PIR e Comandos Remotos:** o ESP32 envia dados periódicos de temperatura, umidade, memória livre (`free_heap`), sinal Wi-Fi (`RSSI`) e tempo de atividade (`uptime`), notifica detecções de movimento do sensor PIR e executa comandos remotos enviados pelo dashboard via MQTT (`open_door`, `close_door`, `unlock`, `reboot`).

---

## Estrutura do Repositório

```text
.
├── access-control/
│   ├── diagram.json                              # Diagrama de conexões e componentes no Wokwi
│   ├── libraries.txt                             # Bibliotecas utilizadas no firmware ESP32
│   ├── sketch.ino                                # Código-fonte C++/Arduino do ESP32
│   └── wokwi-project.txt                         # Identificação do projeto no Wokwi
├── public/
│   └── index.html                                # Dashboard web de monitoramento e controle
├── database.sql                                  # Script SQL de criação do banco e tabelas
├── guia_de_conexao_e_execucao_do_servidor.md     # Guia detalhado de configuração e execução
├── server.js                                     # Backend Node.js (Express, Socket.IO, MQTT e MySQL)
└── README.md                                     # Documentação principal do repositório
```

---

## Pré-requisitos

* **Node.js** (v18 ou superior) e **npm**
* **MySQL Server** (ou MariaDB) ativo
* **Cloudflared** instalado (para criar túnel HTTPS público apontando para o servidor local durante a simulação no Wokwi)
* Ambiente **Wokwi** (navegador web ou extensão no VS Code)

---

## Como Executar o Projeto

### 1. Configurar o Banco de Dados MySQL
Certifique-se de que o serviço MySQL está em execução e importe o script `database.sql`:

```bash
mysql -u root -p < database.sql
```

### 2. Configurar as Variáveis de Ambiente (`.env`)
Crie um arquivo `.env` na raiz do projeto (não versionado no Git) com os parâmetros de conexão:

```env
PORT=3001
DB_HOST=localhost
DB_USER=access_user
DB_PASSWORD=access_pass
DB_NAME=access_control
MQTT_BROKER=mqtt://broker.hivemq.com:1883
```

### 3. Instalar Dependências e Iniciar o Backend
Na raiz do repositório, instale os pacotes necessários e inicie o servidor Node.js:

```bash
npm init -y
npm install express socket.io mysql2 mqtt cors dotenv
node server.js
```

O servidor ficará disponível localmente em `http://localhost:3001`.

### 4. Expor a API Local com Cloudflare Tunnel
Em um segundo terminal, inicie o túnel forçando o protocolo HTTP/2 para maior estabilidade:

```bash
cloudflared tunnel --url http://localhost:3001 --protocol http2
```

Copie a URL pública gerada (`https://....trycloudflare.com`).

### 5. Configurar e Executar o ESP32 no Wokwi
1. Abra os arquivos da pasta `access-control/` no **Wokwi**.
2. No arquivo `sketch.ino`, atualize a constante `API_URL` com a URL do túnel seguida de `/api/access`:
   ```cpp
   const char* API_URL = "https://SUA-URL-ATIVA.trycloudflare.com/api/access";
   ```
3. Salve o projeto e inicie a simulação (**Play**).
4. Acesse `http://localhost:3001` no navegador para acompanhar os eventos, cadastrar novos cartões RFID e enviar comandos remotos.

> Para instruções detalhadas de verificação e solução de problemas, consulte o arquivo [`guia_de_conexao_e_execucao_do_servidor.md`](guia_de_conexao_e_execucao_do_servidor.md).

---

## Entregáveis Consolidados

* **Código-fonte e Documentação:** disponíveis neste repositório (`Betorbb/Access-Control-IOT`).
* **Vídeo Demonstrativo:** gravação com o fluxo completo ponta a ponta (leitura de cartão autorizado e não autorizado, cadastro/persistência no banco, telemetria/PIR e comandos remotos).
* **Relatório Técnico Final:** documentação consolidada contendo arquitetura, modelagem de dados, implementação, testes de validação e evoluções futuras.