FROM eclipse-temurin:17-jre

ARG LESHAN_VERSION=2.0.0-M17
ARG LESHAN_SHA256=0eb7f583376d6547f6214c836ec5e172c9ee46f6cb4e98684bcaf33e49b64f3a

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl \
    && curl -fsSL -o /opt/leshan-demo-server.jar \
       "https://repo1.maven.org/maven2/org/eclipse/leshan/leshan-demo-server/${LESHAN_VERSION}/leshan-demo-server-${LESHAN_VERSION}-jar-with-dependencies.jar" \
    && echo "${LESHAN_SHA256}  /opt/leshan-demo-server.jar" | sha256sum -c - \
    && rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["java", "-jar", "/opt/leshan-demo-server.jar"]
