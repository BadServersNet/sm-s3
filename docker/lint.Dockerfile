FROM debian:trixie-slim

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		ca-certificates \
		clang-format-19 \
		clang-tidy-19 \
		g++ \
		nodejs \
		npm \
		pipx \
		shellcheck \
		shfmt \
	&& rm -rf /var/lib/apt/lists/* \
	&& ln -s /usr/bin/clang-format-19 /usr/local/bin/clang-format \
	&& ln -s /usr/bin/clang-tidy-19 /usr/local/bin/clang-tidy

ENV PIPX_HOME=/opt/pipx PIPX_BIN_DIR=/usr/local/bin

RUN pipx install gersemi==0.19.3 \
	&& npm install --global oxfmt@0.68.0 \
	&& npm cache clean --force

WORKDIR /src
