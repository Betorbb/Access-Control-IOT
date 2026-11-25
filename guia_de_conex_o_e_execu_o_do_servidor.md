# Guia de Execução e Conexão — Sistema de Controle de Acesso IoT

Este documento descreve o procedimento completo para configurar o banco de dados, inicializar o servidor backend, expor a API localmente via Cloudflare Tunnel e conectar o dispositivo ESP32 simulado no Wokwi.

## 1. Estrutura do Pacote (`entrega-3`)

Ao descompactar o arquivo `entrega-3.zip`, você encontrará a seguinte organização:

* `server.js`: Servidor backend em Node.js (Express, Socket.IO, MQTT e MySQL)
* `database.sql`: Script SQL para criação do banco de dados e das tabelas
* `.env`: Arquivo com as variáveis de ambiente (porta, credenciais do MySQL e broker MQTT)
* `public/index.html`: Interface web do Dashboard de monitoramento e controle
* `access-control/`: Arquivos do projeto do ESP32 no Wokwi (`sketch.ino`, `diagram.json`, `libraries.txt`, `wokwi-project.txt`)
* `Access control.mp4`: Vídeo demonstrativo do funcionamento completo do sistema
* `GUIA_CONEXAO_SERVIDOR.md`: Este guia de instalação e execução

## 2. Pré-requisitos

Certifique-se de que os seguintes serviços e ferramentas estão instalados no ambiente:

* **Node.js** (v18 ou superior) e **npm**
* **MySQL / MariaDB** ativo no sistema
* **Cloudflared** instalado (`cloudflared --version`)
* Acesso ao **Wokwi** (navegador web ou extensão Wokwi no VS Code)

## 3. Configurar e Verificar o Banco de Dados MySQL

1. Confirme se o serviço do MySQL está ativo no terminal:

   ```bash
   sudo systemctl status mysql
   ```

   *(Caso esteja inativo, inicie com `sudo systemctl start mysql`)*

2. Acesse o diretório onde o pacote `entrega-3` foi extraído e importe o script `database.sql`:

   ```bash
   cd /caminho/onde/extraiu/entrega-3
   sudo mysql < database.sql
   ```

3. Verifique se o banco `access_control` e a tabela `authorized_cards` estão acessíveis:

   ```bash
   mysql -u access_user -paccess_pass access_control -e "SELECT * FROM authorized_cards;"
   ```

## 4. Iniciar o Servidor Backend (Node.js)

1. Abra um terminal e navegue até a pasta `entrega-3` no local onde você extraiu os arquivos:

   ```bash
   cd /caminho/onde/extraiu/entrega-3
   ```

2. Instale as dependências do projeto (caso a pasta `node_modules` ainda não exista):

   ```bash
   npm init -y
   npm install express socket.io mysql2 mqtt cors dotenv
   ```

3. Inicie o servidor:

   ```bash
   node server.js
   ```

4. **Verificação:** O terminal deve exibir as mensagens abaixo e permanecer aberto:

   ```text
   Servidor rodando em http://localhost:3001
   Conectado ao Broker MQTT: mqtt://broker.hivemq.com:1883
   ```

> **Importante:** Mantenha este terminal aberto durante toda a utilização do sistema.

## 5. Criar o Túnel HTTPS com Cloudflare Tunnel

Como o simulador Wokwi roda na nuvem, ele precisa de uma URL pública HTTPS para acessar a rota `POST /api/access` que está rodando localmente na porta `3001`.

1. Abra um **segundo terminal** e execute o comando abaixo (utilizando `--protocol http2` para maior estabilidade de conexão):

   ```bash
   cloudflared tunnel --url http://localhost:3001 --protocol http2
   ```

2. Aguarde a criação do túnel. O terminal exibirá uma caixa com a URL pública gerada dinamicamente, semelhante a:

   ```text
   +--------------------------------------------------------------------------------------------+
   |  Your quick Tunnel has been created! Visit it at (it may take some time to be reachable):  |
   |  https://exemplo-aleatorio.trycloudflare.com                                               |
   +--------------------------------------------------------------------------------------------+
   ```

3. **Verificação:** Confirme nas últimas linhas do log se apareceu a mensagem `Registered tunnel connection`. Deixe este segundo terminal aberto.

> **Atenção:** Toda vez que o `cloudflared` for encerrado e iniciado novamente, uma **nova URL** será gerada, exigindo a atualização no código do ESP32.

## 6. Configurar e Conectar o ESP32 no Wokwi

1. Abra o projeto no **Wokwi** (carregando os arquivos da pasta `access-control/`: `sketch.ino`, `diagram.json` e `libraries.txt`).

2. No arquivo `sketch.ino`, localize a constante `API_URL` (linha 20) e substitua pelo endereço gerado no Passo 5, mantendo `/api/access` no final:

   ```cpp
   const char* API_URL = "https://exemplo-aleatorio.trycloudflare.com/api/access";
   ```

3. Clique em **SALVAR** no topo do editor do Wokwi.

4. Inicie a simulação clicando no botão **Play** (ou **Stop** e **Play** para reiniciar).

5. **Verificação:** Acompanhe o **Monitor Série** do Wokwi e confirme as mensagens:

   ```text
   RFID RC522 OK
   Conectando WiFi....
   WiFi conectado! IP: 10.13.37.2
   Conectando MQTT... Conectado!
   Setup completo!
   ```

## 7. Acessar o Dashboard e Validar o Funcionamento

1. Abra o navegador e acesse o painel web localmente:

   ```text
   http://localhost:3001
   ```

2. **Teste de Cartão Autorizado:** No Wokwi, selecione uma tag cadastrada e ativa no banco (ex.: `01020304` ou `11223344`) no leitor MFRC522 e clique em **TAP**.
   * **Resultado esperado:** O Monitor Série exibe `HTTP codigo: 200` e `Acesso LIBERADO`, o LED verde acende, o relé ativa por 5 segundos e o evento aparece em tempo real no Dashboard.

3. **Teste de Cartão Não Autorizado:** Passe um cartão que não esteja cadastrado ou esteja inativo no banco.
   * **Resultado esperado:** O Monitor Série exibe `HTTP codigo: 200` e `Acesso NEGADO`, o LED vermelho pisca junto ao buzzer e o evento de acesso negado é registrado no painel.

4. **Cadastro de Novo Cartão e Comandos Remotos:** Utilize o formulário do Dashboard para cadastrar um novo UID e teste os botões de comando remoto (`Abrir Porta`, `Fechar Porta`, `Destravar 5s`) para validar a comunicação bidirecional via MQTT.

## 8. Solução Rápida de Problemas (Troubleshooting)

* **Erro HTTP `-1 (connection refused)` ou `530`:** O túnel `cloudflared` foi encerrado ou reiniciado. Gere uma nova URL no terminal do `cloudflared`, atualize `API_URL` no `sketch.ino`, salve e reinicie a simulação.
* **Erro HTTP `404`:** Verifique se `API_URL` termina exatamente com `/api/access` (sem barras extras ou duplicações) e se o `server.js` está em execução.
* **Falha ao conectar Wi-Fi (`Status: 0`):** Clique em **Stop** no Wokwi, recarregue a página do navegador (`F5`) para limpar o estado da rede virtual e clique em **Play** novamente.