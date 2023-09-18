ARG REGISTRY_MIRROR_LIB=""

FROM ${REGISTRY_MIRROR_LIB}postgres:16.0-alpine AS extension-builder
# Build process requires installed postgres

RUN apk add git make alpine-sdk clang15 llvm15

WORKDIR /iban
COPY . .
RUN make
RUN make install

FROM ${REGISTRY_MIRROR_LIB}postgres:16.0-alpine
# Setup final image with extensions

# iban
COPY --from=extension-builder /usr/local/share/postgresql/extension/iban* /usr/local/share/postgresql/extension/
COPY --from=extension-builder /usr/local/lib/postgresql/iban.so /usr/local/lib/postgresql/iban.so
COPY --from=extension-builder /usr/local/lib/postgresql/bitcode/iban.index.bc /usr/local/lib/postgresql/bitcode/iban.index.bc
COPY --from=extension-builder /usr/local/lib/postgresql/bitcode/iban/ /usr/local/lib/postgresql/bitcode/iban/
