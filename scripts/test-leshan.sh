#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

docker compose --profile leshan down leshan leshan-client
docker compose --profile leshan up --detach leshan

status=0
docker compose --profile leshan run --rm leshan-client || status=$?

docker compose --profile leshan down leshan leshan-client

exit $status
