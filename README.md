# Servidor HTTP Multi-Processo Concorrente

Projeto desenvolvido no ambito da unidade curricular **Sistemas Operativos**.  
Implementa um servidor HTTP/1.1 concorrente em **C**, utilizando uma arquitetura **master–worker** com múltiplos processos, pools de threads e mecanismos de IPC baseados em **memória partilhada e semáforos POSIX**.

## Funcionalidades Principais

- Suporte HTTP/1.1 (métodos `GET` e `HEAD`)
- Arquitetura prefork com múltiplos workers
- Pool de threads por worker
- Fila limitada producer–consumer (backpressure)
- Cache LRU de ficheiros estáticos
- Estatísticas globais em memória partilhada
- Endpoints de monitorização:
  - `/health`
  - `/metrics`
  - `/stats`
- Encerramento gracioso (graceful shutdown)

## Estrutura do Projeto

```

.
├── src/                # Código-fonte
├── tests/              # Testes de stresse e desempenho
├── www/                # Document root (ficheiros estáticos)
├── server.conf         # Ficheiro de configuração
├── Makefile
├── Dockerfile
├── docker-compose.yml
└── README.md

```

## Requisitos

- Linux (compatível com POSIX)
- GCC ≥ 7.0
- GNU Make
- Bibliotecas POSIX (`pthread`, `librt`)

Em sistemas Debian/Ubuntu:

```bash
sudo apt-get install build-essential
```

## Quick Start

### 1. Compilar

```bash
make
```

O binário será gerado em:

```
bin/concurrent-http-server
```

### 2. Configurar

Editar o ficheiro `server.conf` (opcional):

```ini
PORT=8080
DOCUMENT_ROOT=./www
NUM_WORKERS=4
THREADS_PER_WORKER=10
TIMEOUT_SECONDS=30
CACHE_SIZE_MB=10
```

### 3. Executar

```bash
./bin/concurrent-http-server server.conf
```

### 4. Testar

```bash
curl http://localhost:8080/
curl http://localhost:8080/health
curl http://localhost:8080/metrics
curl http://localhost:8080/stats
```

## Execução com Docker (Opcional)

```bash
docker build -t http-server .
docker run -p 8080:8080 -v ./www:/app/www http-server
```

Ou com Docker Compose:

```bash
docker-compose up -d
docker-compose logs -f
```

---

## Encerramento do Servidor

Encerramento gracioso:

```bash
Ctrl+C
```

Ou:

```bash
kill -SIGINT <pid>
```

## Documentação

A documentação do projeto encontra-se dividida em três documentos:

* **Relatório Técnico** — implementação e resultados
* **Documento de Design** — arquitetura e decisões
* **Manual de Utilização** — instruções detalhadas de uso

Estão disponíveis na pasta `docs/`.