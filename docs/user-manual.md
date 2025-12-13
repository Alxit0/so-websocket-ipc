# 📗 Manual do Usuário — Servidor HTTP Multi-Processo

## 1. Visão Geral

Servidor HTTP/1.1 de alto desempenho implementado em C que utiliza arquitetura master-worker com múltiplos processos e thread pools para processar requisições concorrentes. O servidor oferece:

- Suporte HTTP/1.1 (métodos GET e HEAD)
- Arquitetura prefork com N workers configuráveis
- Thread pool por worker para máximo paralelismo
- Cache LRU de arquivos estáticos
- Estatísticas globais em tempo real
- Endpoints de monitoramento (/health, /metrics, /stats)
- Graceful shutdown sem perda de dados

**Ideal para:** Servir arquivos estáticos, APIs simples, ambientes de produção com alta concorrência.

---

## 2. Requisitos

### 2.1 Sistema Operacional

- **Linux:** Kernel 3.10+ (testado em Ubuntu 20.04+, Debian 11+, CentOS 7+)
- **POSIX compliant:** Requer suporte a shared memory, semáforos POSIX, pthreads

### 2.2 Compilador e Ferramentas

- **GCC:** Versão 7.0 ou superior
  ```bash
  gcc --version  # Verificar versão
  ```
- **GNU Make:** 4.0+
- **GLIBC:** 2.27+

### 2.3 Bibliotecas Necessárias

**Instalação no Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install build-essential gcc make
```

**Instalação no CentOS/RHEL:**
```bash
sudo yum groupinstall "Development Tools"
sudo yum install gcc make
```

**Bibliotecas linkadas:**
- `pthread` (POSIX threads)
- `rt` (Real-time extensions para shared memory)

### 2.4 Ferramentas Opcionais

- **Valgrind:** Para detecção de memory leaks
  ```bash
  sudo apt-get install valgrind
  ```
- **Docker:** Para deployment containerizado
- **curl/wget:** Para testes manuais
- **Apache Bench (ab):** Para testes de carga

---

## 3. Compilação

### 3.1 Compilação Básica

**Clone o repositório:**
```bash
git clone <repository-url>
cd so-websocket-ipc
```

**Compile o servidor:**
```bash
make
```

**Saída esperada:**
```
gcc -Wall -Wextra -O2 -c -o obj/main.o src/main.c
gcc -Wall -Wextra -O2 -c -o obj/config.o src/config.c
...
gcc -Wall -Wextra -O2 -o bin/concurrent-http-server obj/*.o -pthread -lrt
```

**Binário gerado:** `bin/concurrent-http-server`

### 3.2 Comandos do Makefile

| Comando | Descrição |
|---------|-----------|
| `make` ou `make all` | Compila o servidor (default) |
| `make clean` | Remove binários e objetos compilados |
| `make run` | Compila e executa com `server.conf` |

### 3.3 Compilação Manual (sem Makefile)

```bash
gcc -Wall -Wextra -O2 -pthread -lrt \
    src/main.c \
    src/config.c \
    src/logger.c \
    src/stats.c \
    src/http.c \
    src/thread_pool.c \
    src/connection_queue.c \
    src/server.c \
    src/file_cache.c \
    -o server
```

### 3.4 Build com Docker

```bash
docker build -t http-server .
docker run -p 8080:8080 -v ./www:/app/www http-server
```

---

## 4. Configuração

### 4.1 Arquivo de Configuração

O servidor lê configurações de `server.conf` no formato `CHAVE=VALOR`.

**Exemplo de `server.conf`:**
```ini
PORT=8080
DOCUMENT_ROOT=./www
NUM_WORKERS=4
THREADS_PER_WORKER=8
TIMEOUT_SECONDS=30
CACHE_SIZE_MB=50
```

### 4.2 Parâmetros de Configuração

| Parâmetro | Descrição | Valores | Default |
|-----------|-----------|---------|---------|
| `PORT` | Porta TCP para escuta | 1-65535 | 8080 |
| `DOCUMENT_ROOT` | Diretório raiz dos arquivos | Path absoluto/relativo | ./www |
| `NUM_WORKERS` | Número de processos worker | 1-16 | 4 |
| `THREADS_PER_WORKER` | Threads por worker | 1-32 | 8 |
| `TIMEOUT_SECONDS` | Timeout de socket (recv/send) | 1-300 | 30 |
| `CACHE_SIZE_MB` | Tamanho do cache LRU por worker | 0-1024 | 50 |

### 4.3 Guia de Tuning

**Para servidor com 4 CPUs:**
```ini
NUM_WORKERS=4           # 1 por CPU core
THREADS_PER_WORKER=8    # Total: 32 threads
CACHE_SIZE_MB=50        # 200MB total (4×50)
```

**Para servidor com 8 CPUs:**
```ini
NUM_WORKERS=8
THREADS_PER_WORKER=8
CACHE_SIZE_MB=100
```

**Para máxima performance:**
- `NUM_WORKERS` = número de CPU cores
- `THREADS_PER_WORKER` = 4-16 (testar empiricamente)
- `CACHE_SIZE_MB` = RAM disponível / NUM_WORKERS / 4

**Para baixo consumo de memória:**
```ini
NUM_WORKERS=2
THREADS_PER_WORKER=4
CACHE_SIZE_MB=10
```

### 4.4 Estrutura do Document Root

```
www/
├── index.html          # Página principal
├── style.css
├── script.js
└── assets/
    ├── logo.png
    └── background.jpg
```

**Requisitos:**
- Permissões de leitura para todos os arquivos
- Não seguir symlinks (segurança)
- Arquivos > 1MB não são cacheados

---

## 5. Executando o Servidor

### 5.1 Execução Padrão

**Com arquivo de configuração:**
```bash
./bin/concurrent-http-server server.conf
```

**Sem configuração (usa defaults):**
```bash
./bin/concurrent-http-server
```

**Saída esperada:**
```
[2025-12-12 10:30:45] Master process listening on port 8080
[2025-12-12 10:30:45] Document root: ./www
[2025-12-12 10:30:45] Number of workers: 4
[2025-12-12 10:30:45] Worker 0: Thread 0 started (TID: 12345)
[2025-12-12 10:30:45] Worker 0: Thread 1 started (TID: 12346)
...
```

### 5.2 Execução com Configuração Customizada

**Criar configuração alternativa:**
```bash
cat > production.conf << EOF
PORT=80
DOCUMENT_ROOT=/var/www/html
NUM_WORKERS=8
THREADS_PER_WORKER=16
TIMEOUT_SECONDS=60
CACHE_SIZE_MB=100
EOF
```

**Executar:**
```bash
sudo ./bin/concurrent-http-server production.conf
```

*(Nota: Porta 80 requer privilégios root)*

### 5.3 Modo Background (Daemon)

**Usando nohup:**
```bash
nohup ./bin/concurrent-http-server server.conf > server.log 2>&1 &
echo $! > server.pid
```

**Verificar se está rodando:**
```bash
ps aux | grep concurrent-http-server
tail -f server.log
```

**Parar servidor em background:**
```bash
kill $(cat server.pid)
```

### 5.4 Modo Verbose (Debug)

O servidor loga automaticamente para `server.log`.

**Monitorar logs em tempo real:**
```bash
tail -f server.log
```

**Aumentar verbosidade (recompilar com debug):**
```bash
CFLAGS="-Wall -Wextra -g -DDEBUG" make clean all
./bin/concurrent-http-server server.conf
```

### 5.5 Execução com Docker Compose

**Arquivo:** `docker-compose.yml`

```yaml
services:
  server:
    build: .
    ports:
      - "8080:8080"
    volumes:
      - ./www:/app/www
      - ./server.conf:/app/server.conf
```

**Executar:**
```bash
docker-compose up -d
docker-compose logs -f server
```

---

## 6. Endpoints Disponíveis

### 6.1 Arquivos Estáticos

**Endpoint:** `GET /<path>`

**Exemplos:**
```bash
curl http://localhost:8080/index.html
curl http://localhost:8080/assets/logo.png
```

**Respostas:**
- `200 OK` - Arquivo encontrado
- `404 Not Found` - Arquivo não existe
- `500 Internal Server Error` - Erro de I/O

**Headers enviados:**
```
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: 1234
Server: TemplateHTTP/1.0
Connection: close
```

### 6.2 Endpoint `/health`

**Propósito:** Health check para load balancers e monitoramento.

**Método:** `GET` ou `HEAD`

**Request:**
```bash
curl http://localhost:8080/health
```

**Response:**
```json
{
  "status": "healthy",
  "uptime": 3600,
  "workers": 4
}
```

**Headers:**
```
HTTP/1.1 200 OK
Content-Type: application/json
X-Priority: high
Connection: close
```

**Uso:** Configure seu load balancer (Nginx, HAProxy) para fazer health checks neste endpoint.

### 6.3 Endpoint `/metrics`

**Propósito:** Métricas estilo Prometheus para monitoramento.

**Método:** `GET`

**Request:**
```bash
curl http://localhost:8080/metrics
```

**Response (formato Prometheus):**
```
# HELP http_requests_total Total HTTP requests
# TYPE http_requests_total counter
http_requests_total 12543

# HELP http_bytes_sent_total Total bytes sent
# TYPE http_bytes_sent_total counter
http_bytes_sent_total 45123456

# HELP http_requests_by_code HTTP requests by status code
# TYPE http_requests_by_code counter
http_requests_by_code{code="200"} 12000
http_requests_by_code{code="404"} 543
http_requests_by_code{code="500"} 0

# HELP http_active_connections Active connections
# TYPE http_active_connections gauge
http_active_connections 8

# HELP http_avg_response_time_ms Average response time
# TYPE http_avg_response_time_ms gauge
http_avg_response_time_ms 42
```

**Integração Prometheus:**
```yaml
scrape_configs:
  - job_name: 'http-server'
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: '/metrics'
```

### 6.4 Endpoint `/stats`

**Propósito:** JSON detalhado com estatísticas do servidor.

**Método:** `GET`

**Request:**
```bash
curl http://localhost:8080/stats
```

**Response:**
```json
{
  "total_requests": 12543,
  "bytes_sent": 45123456,
  "http_codes": {
    "200": 12000,
    "404": 543,
    "500": 0
  },
  "active_connections": 8,
  "avg_response_time_ms": 42.5
}
```

### 6.5 Páginas de Erro

**404 Not Found:**
```html
<html><body>
<h1>404 Not Found</h1>
<p>The requested resource was not found.</p>
</body></html>
```

**500 Internal Server Error:**
```html
<html><body>
<h1>500 Internal Server Error</h1>
<p>An internal error occurred.</p>
</body></html>
```

**503 Service Unavailable (fila cheia):**
```html
<html><body>
<h1>503 Service Unavailable</h1>
<p>Server is overloaded. Please try again later.</p>
</body></html>
```

**Headers em 503:**
```
HTTP/1.1 503 Service Unavailable
Retry-After: 1
Connection: close
```

---

## 7. Parando o Servidor

### 7.1 Graceful Shutdown com SIGINT

**Terminal interativo:**
```bash
# Pressionar Ctrl+C
```

**Em background:**
```bash
kill -SIGINT $(pidof concurrent-http-server)
```

**Comportamento:**
1. Master process recebe SIGINT
2. Envia SIGTERM para todos os workers
3. Workers param de aceitar novas conexões
4. Workers aguardam threads terminarem
5. Cleanup de recursos (shared memory, cache)
6. Exit com código 0

### 7.2 Shutdown com SIGTERM

```bash
kill -SIGTERM $(pidof concurrent-http-server)
```

**Equivalente ao SIGINT:** Graceful shutdown.

### 7.3 Force Kill (Não Recomendado)

```bash
kill -9 $(pidof concurrent-http-server)
```

**Consequências:**
- Shared memory pode não ser limpa
- Arquivos de log podem ficar corrompidos
- Conexões ativas são abortadas

**Cleanup manual após force kill:**
```bash
# Remover shared memory órfã
ipcs -m | grep $(whoami) | awk '{print $2}' | xargs -n1 ipcrm -m

# Remover semáforos órfãos
ipcs -s | grep $(whoami) | awk '{print $2}' | xargs -n1 ipcrm -s
```

### 7.4 Verificação de Shutdown

**Confirmar que servidor parou:**
```bash
ps aux | grep concurrent-http-server
netstat -tuln | grep 8080
```

**Verificar logs:**
```bash
tail -20 server.log
```

**Saída esperada no log:**
```
[2025-12-12 15:30:00] Worker 0: Thread 0 exiting
[2025-12-12 15:30:00] Worker 0: Thread 1 exiting
...
[2025-12-12 15:30:01] Master process shutting down
```

---

## 8. Troubleshooting

### 8.1 "Address already in use"

**Problema:**
```
bind failed: Address already in use
```

**Causa:** Porta 8080 já está em uso por outro processo.

**Solução 1 - Encontrar processo:**
```bash
sudo netstat -tulpn | grep :8080
# Ou
sudo lsof -i :8080
```

**Solução 2 - Matar processo:**
```bash
kill $(lsof -t -i:8080)
```

**Solução 3 - Usar porta diferente:**
```ini
# server.conf
PORT=8081
```

**Solução 4 - TIME_WAIT timeout:**
```bash
# Aguardar 60 segundos para kernel liberar porta
sleep 60
./bin/concurrent-http-server server.conf
```

### 8.2 Corrupção do Arquivo de Log

**Problema:** Log ilegível ou caracteres estranhos.

**Causa:** Múltiplas threads escrevendo simultaneamente sem sincronização.

**Verificação:**
```bash
file server.log
# Deve retornar: ASCII text
```

**Solução 1 - Remover e reiniciar:**
```bash
rm server.log
./bin/concurrent-http-server server.conf
```

**Solução 2 - Rodar com Valgrind para detectar race conditions:**
```bash
valgrind --tool=helgrind ./bin/concurrent-http-server server.conf
```

**Prevenção:** O código atual já usa `pthread_mutex` para proteger escritas no log.

### 8.3 Problemas de Performance

#### 8.3.1 Alta Latência

**Sintoma:** Response time > 500ms

**Diagnóstico:**
```bash
curl -w "@curl-format.txt" -o /dev/null -s http://localhost:8080/index.html
```

**curl-format.txt:**
```
time_namelookup:  %{time_namelookup}\n
time_connect:     %{time_connect}\n
time_starttransfer: %{time_starttransfer}\n
time_total:       %{time_total}\n
```

**Soluções:**
- Aumentar `THREADS_PER_WORKER`
- Aumentar `CACHE_SIZE_MB`
- Verificar I/O de disco com `iostat -x 1`

#### 8.3.2 Taxa Alta de 503 Responses

**Sintoma:** Muitos erros "Service Unavailable"

**Causa:** Fila de conexões cheia (100 slots).

**Soluções:**
```ini
# Aumentar paralelismo
NUM_WORKERS=8
THREADS_PER_WORKER=16

# Ou modificar código:
# connection_queue.h: #define QUEUE_SIZE 200
```

#### 8.3.3 Alto Uso de CPU

**Diagnóstico:**
```bash
top -H -p $(pidof concurrent-http-server)
```

**Soluções:**
- Reduzir `THREADS_PER_WORKER`
- Verificar spin locks com `perf record`
- Ativar `SO_REUSEPORT` (já habilitado)

#### 8.3.4 Alto Uso de Memória

**Diagnóstico:**
```bash
ps aux | grep concurrent-http-server
# Verificar coluna RSS (Resident Set Size)
```

**Soluções:**
```ini
# Reduzir cache
CACHE_SIZE_MB=10

# Reduzir workers
NUM_WORKERS=2
```

**Verificar leaks:**
```bash
valgrind --leak-check=full ./bin/concurrent-http-server server.conf
# Testar com algumas requisições
curl http://localhost:8080/index.html
# Parar servidor (Ctrl+C)
```

### 8.4 Permission Denied em Document Root

**Problema:**
```
[ERROR] Failed to open file: Permission denied
```

**Solução:**
```bash
chmod 755 www/
chmod 644 www/*.html
```

### 8.5 Servidor Não Responde

**Diagnóstico:**
```bash
# Verificar se processo está rodando
ps aux | grep concurrent-http-server

# Verificar se porta está escutando
netstat -tuln | grep 8080

# Testar conectividade
telnet localhost 8080

# Verificar firewall
sudo iptables -L -n | grep 8080
```

**Soluções:**
- Verificar logs: `tail -50 server.log`
- Reiniciar servidor
- Verificar configuração de firewall

### 8.6 Deadlock (Servidor Trava)

**Sintoma:** Servidor para de responder, CPU em 0%.

**Diagnóstico:**
```bash
# Attach GDB
gdb -p $(pidof concurrent-http-server)
(gdb) thread apply all bt
```

**Solução temporária:**
```bash
kill -9 $(pidof concurrent-http-server)
./bin/concurrent-http-server server.conf
```

**Reporte o bug com:** Stack traces de todos os threads.

---

## Apêndice A: Exemplos de Uso

### A.1 Teste Básico

```bash
# Terminal 1: Start server
./bin/concurrent-http-server server.conf

# Terminal 2: Test endpoints
curl http://localhost:8080/index.html
curl http://localhost:8080/health
curl http://localhost:8080/metrics
```

### A.2 Load Test

```bash
# Apache Bench
ab -n 10000 -c 100 http://localhost:8080/index.html

# K6 (se disponível)
k6 run tests/loadtest.js
```

### A.3 Integração com Nginx

**nginx.conf:**
```nginx
upstream backend {
    server localhost:8080 max_fails=3 fail_timeout=30s;
    server localhost:8081 max_fails=3 fail_timeout=30s;
}

server {
    listen 80;
    
    location / {
        proxy_pass http://backend;
        proxy_set_header Host $host;
    }
    
    location /health {
        proxy_pass http://backend/health;
        access_log off;
    }
}
```

---

## Apêndice B: Referências Rápidas

**Comandos essenciais:**
```bash
make                     # Compilar
make clean              # Limpar build
make run                # Compilar + executar
./bin/concurrent-http-server server.conf  # Executar
kill -SIGINT $(pidof concurrent-http-server)  # Parar
tail -f server.log      # Monitorar logs
```

**Arquivos importantes:**
- `bin/concurrent-http-server` - Binário executável
- `server.conf` - Configuração
- `server.log` - Logs do servidor
- `www/` - Document root

**Portas padrão:**
- `8080` - Servidor HTTP
- `9090` - Prometheus (opcional)
- `3000` - Grafana (opcional)
