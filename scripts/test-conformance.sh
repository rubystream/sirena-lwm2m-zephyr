#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

PROFILE=conformance
LESHAN_SERVER=leshan
LESHAN_CLIENT=leshan-client
SIRENA_SERVER=sirena-lwm2m2
SIRENA_CLIENT=sirena-client

remove_services() {
    server="$1"
    client="$2"

    docker compose --profile "$PROFILE" stop "$server" "$client" || true
    docker compose --profile "$PROFILE" rm -f "$server" "$client" || true
    docker compose --profile "$PROFILE" down || true
}

run_tier() {
    server="$1"
    client="$2"
    label="$3"
    status=0

    echo "conformance: the $label tier starts"
    remove_services "$server" "$client"
    docker compose --profile "$PROFILE" up --detach "$server"
    docker compose --profile "$PROFILE" run --rm "$client" || status=$?
    remove_services "$server" "$client"

    if [ "$status" -ne 0 ]; then
        echo "conformance: the $label tier failed"
    else
        echo "conformance: the $label tier passed"
    fi

    return $status
}

status=0

case "${1:-all}" in
    leshan)
        run_tier "$LESHAN_SERVER" "$LESHAN_CLIENT" "Leshan" || status=$?
        ;;
    sirena)
        run_tier "$SIRENA_SERVER" "$SIRENA_CLIENT" "Sirena" || status=$?
        ;;
    all)
        run_tier "$LESHAN_SERVER" "$LESHAN_CLIENT" "Leshan" || status=$?
        run_tier "$SIRENA_SERVER" "$SIRENA_CLIENT" "Sirena" || status=$?
        ;;
    *)
        echo "conformance: the tier ${1} is unknown"
        exit 2
        ;;
esac

exit $status
