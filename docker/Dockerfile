# ---- Builder stage: compile dcat ----
FROM debian:bookworm-slim AS builder

RUN sed -i 's|deb.debian.org|mirrors.tuna.tsinghua.edu.cn|g; s|security.debian.org|mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list.d/debian.sources 2>/dev/null; \
    sed -i 's|deb.debian.org|mirrors.tuna.tsinghua.edu.cn|g; s|security.debian.org|mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list 2>/dev/null; \
    apt-get update && \
    apt-get install -y --no-install-recommends cmake gcc make libc6-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN mkdir -p build && cd build && cmake .. && make -j"$(nproc)"

# ---- Runtime stage ----
FROM debian:bookworm-slim

RUN sed -i 's|deb.debian.org|mirrors.tuna.tsinghua.edu.cn|g; s|security.debian.org|mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list.d/debian.sources 2>/dev/null; \
    sed -i 's|deb.debian.org|mirrors.tuna.tsinghua.edu.cn|g; s|security.debian.org|mirrors.tuna.tsinghua.edu.cn|g' /etc/apt/sources.list 2>/dev/null; \
    apt-get update && \
    apt-get install -y --no-install-recommends \
      iproute2 ethtool iptables perl python3 util-linux coreutils procps \
      ipmitool smartmontools dmidecode \
      pciutils sysstat dmsetup e2fsprogs \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/   /app/build/
COPY --from=builder /app/config/  /app/config/
COPY --from=builder /app/src/scripts/ /app/src/scripts/
COPY --from=builder /app/src/web/     /app/src/web/
COPY --from=builder /app/plugins/    /app/plugins/

RUN ln -sf /app/build/dcat /usr/local/bin/dcat

ENV HOME=/var/lib/demoncat
VOLUME /var/lib/demoncat
EXPOSE 8080

ENTRYPOINT ["dcat"]
CMD ["serve", "--port", "8080"]
