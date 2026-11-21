FROM eclipse-temurin:17-jre

ARG LESHAN_VERSION=2.0.0-M18
ARG LESHAN_SHA256=734b5b8487aa208847b676e7f543744998c6b6ab7bc154551220336d633b02da

RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl \
    && curl -fsSL -o /opt/leshan-demo-server.jar \
       "https://repo1.maven.org/maven2/org/eclipse/leshan/leshan-demo-server/${LESHAN_VERSION}/leshan-demo-server-${LESHAN_VERSION}-jar-with-dependencies.jar" \
    && echo "${LESHAN_SHA256}  /opt/leshan-demo-server.jar" | sha256sum -c - \
    && rm -rf /var/lib/apt/lists/*

ENTRYPOINT ["java", "-jar", "/opt/leshan-demo-server.jar"]
