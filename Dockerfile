# Astro AI: one small container (C++ server + web app). No Python, no Node at runtime.
#   docker build -t astro-ai .
#   docker run -d -p 8080:8080 -v astro-data:/data --name astro-ai astro-ai
# Open http://your-host:8080 and create your password on first visit.
# Optional: -e GROQ_API_KEY=... -e ATRIA_API_KEY=... (or paste keys in Settings -> Providers & keys)
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ make pkg-config libcurl4-openssl-dev libssl-dev ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY Makefile workspace_server.cpp platform.h *.inc ./
RUN make workspace_server

FROM debian:bookworm-slim
# poppler-utils gives pdftotext, so PDFs can be read; unzip is used for DOCX.
RUN apt-get update && apt-get install -y --no-install-recommends libcurl4 libssl3 ca-certificates poppler-utils unzip && rm -rf /var/lib/apt/lists/* \
 && useradd --system --home /app --uid 10001 astro && mkdir -p /data && chown astro /data
WORKDIR /app
COPY --from=build /src/workspace_server ./workspace_server
COPY providers.json ./providers.json
COPY web ./web
USER astro
ENV PORT=8080 ASTRO_DATA_DIR=/data
VOLUME /data
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s CMD ["/bin/sh", "-c", "exec 3<>/dev/tcp/127.0.0.1/8080 || exit 1"]
CMD ["./workspace_server"]
