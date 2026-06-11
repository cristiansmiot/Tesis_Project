#!/usr/bin/env bash
# Genera la CA propia y el certificado del broker Mosquitto.
#
# ¿Por qué CA propia y no Let's Encrypt? El broker se expone por el proxy
# TCP de Railway (shinkansen.proxy.rlwy.net), un dominio que no controlamos:
# ninguna CA pública nos emitiría un certificado para él. Con CA propia el
# backend y el firmware "anclan" (pin) la CA, lo que además evita ataques
# MitM con certificados de CAs comprometidas.
#
# Uso:  ./generar_certificados.sh [host_proxy]
#       host_proxy por defecto: shinkansen.proxy.rlwy.net
#
# Salidas (en este directorio):
#   ca.crt / ca.key         — CA raíz (ca.key NO sale de la máquina del tesista)
#   server.crt / server.key — certificado del broker (subir al volumen Railway)
#
# Tras generar: pegar el contenido de ca.crt en
#   - firmware_SmartMeter/config/mqtt_ca_cert.h  (string PEM)
#   - variable MQTT_TLS_CA_FILE del backend (ruta al ca.crt desplegado)

set -euo pipefail

HOST="${1:-shinkansen.proxy.rlwy.net}"
DIAS_CA=1825      # 5 años: vida del proyecto + margen
DIAS_SERVER=825   # < 39 meses, límite que aceptan la mayoría de stacks TLS

echo "Generando CA propia..."
openssl genrsa -out ca.key 4096
openssl req -x509 -new -key ca.key -sha256 -days "$DIAS_CA" -out ca.crt \
  -subj "/C=CO/O=Tesis SmartMeter PUJ/CN=SmartMeter Root CA"

echo "Generando clave y CSR del broker para $HOST ..."
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr \
  -subj "/C=CO/O=Tesis SmartMeter PUJ/CN=$HOST"

# SAN obligatorio: paho (backend) valida hostname contra SAN, no contra CN.
cat > server.ext <<EOF
subjectAltName = DNS:$HOST, DNS:localhost
extendedKeyUsage = serverAuth
keyUsage = digitalSignature, keyEncipherment
EOF

openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -days "$DIAS_SERVER" -sha256 -out server.crt -extfile server.ext

rm -f server.csr server.ext

echo
echo "Verificación:"
openssl verify -CAfile ca.crt server.crt
openssl x509 -in server.crt -noout -subject -ext subjectAltName -dates

echo
echo "Listo. Subir server.crt/server.key/ca.crt al volumen del broker"
echo "(ver TLS_DEPLOY.md). ca.key se queda en esta máquina, fuera de git."
