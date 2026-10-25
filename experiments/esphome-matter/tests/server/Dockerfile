FROM python:3.12-slim

# The ConnectedHomeIP controller wheel uses /data for platform configuration.
# A container keeps its factory settings isolated from the development host.
RUN --mount=type=secret,id=ca_bundle,required=true \
    apt-get -o Acquire::https::CaInfo=/run/secrets/ca_bundle update \
    && apt-get -o Acquire::https::CaInfo=/run/secrets/ca_bundle install -y --no-install-recommends libglib2.0-0t64 libnl-route-3-200 \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir /data && chmod 1777 /data
