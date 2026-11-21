#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

docker compose --profile leshan down --remove-orphans
docker compose --profile leshan up --detach leshan

status=0
docker compose --profile leshan run --rm leshan-client || status=$?

docker compose --profile leshan down

exit $status
