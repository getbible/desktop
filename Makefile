SHELL := /usr/bin/env bash
BUILD_DIR ?= build
BUILD_TYPE ?= Release
PREFIX ?= /usr/local
WEBAPP_CONFIG_FILE ?= config/app.json

.PHONY: configure-app bootstrap configure build run install snap macos macos-dmg windows windows-msix validate clean

configure-app:
	WEBAPP_CONFIG_FILE="$(WEBAPP_CONFIG_FILE)" ./scripts/configure-app.sh "$(WEBAPP_CONFIG_FILE)"

bootstrap:
	./scripts/bootstrap.sh

configure: bootstrap configure-app
	cmake -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DWEBAPP_USE_GENERATED=ON

build: configure
	cmake --build "$(BUILD_DIR)" --parallel

run: build
	BUILD_DIR="$(BUILD_DIR)" WEBAPP_CONFIG_FILE="$(WEBAPP_CONFIG_FILE)" ./scripts/run.sh

install: build
	cmake --install "$(BUILD_DIR)" --prefix "$(PREFIX)"

snap: bootstrap configure-app
	snapcraft

macos:
	WEBAPP_CONFIG_FILE="$(WEBAPP_CONFIG_FILE)" ./scripts/build-macos.sh

macos-dmg:
	WEBAPP_CONFIG_FILE="$(WEBAPP_CONFIG_FILE)" ./scripts/package-macos.sh

windows:
	pwsh -File ./scripts/build-windows.ps1 -ConfigFile "$(WEBAPP_CONFIG_FILE)"

windows-msix:
	pwsh -File ./scripts/package-windows.ps1 -ConfigFile "$(WEBAPP_CONFIG_FILE)"

validate:
	WEBAPP_CONFIG_FILE="$(WEBAPP_CONFIG_FILE)" ./scripts/validate.sh

clean:
	rm -rf "$(BUILD_DIR)" build-* dist stage prime parts *.snap *.msix *.dmg
