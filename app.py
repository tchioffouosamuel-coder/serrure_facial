"""
Service de reconnaissance faciale - deploiement Railway
---------------------------------------------------------
Sert une page web (ouverte dans le navigateur du telephone)
qui capture la camera et envoie des images via HTTPS.
Quand un visage autorise est reconnu, publie un message MQTT
sur EMQX Cloud pour declencher l'ouverture via l'ESP32.

Variables d'environnement a definir sur Railway (Settings > Variables) :
    MQTT_HOST   : endpoint EMQX Cloud (ex: xxxxx.ala.eu-central-1.emqxsl.com)
    MQTT_PORT   : 8883 (TLS) en general
    MQTT_USER   : utilisateur cree dans Access Control sur EMQX Cloud
    MQTT_PASS   : mot de passe correspondant
    MQTT_TOPIC  : topic ecoute par l'ESP32 (ex: lock/unlock)
"""

import os
import time

import numpy as np
from PIL import Image
from flask import Flask, request, jsonify
import face_recognition
import paho.mqtt.client as mqtt

app = Flask(__name__)

# === CONFIGURATION via variables d'environnement ===
MQTT_HOST  = os.environ.get("MQTT_HOST", "")
MQTT_PORT  = int(os.environ.get("MQTT_PORT", 8883))
MQTT_USER  = os.environ.get("MQTT_USER", "")
MQTT_PASS  = os.environ.get("MQTT_PASS", "")
MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "lock/unlock")

TOLERANCE        = 0.5
UNLOCK_COOLDOWN  = 10  # secondes minimum entre deux deverrouillages
FACES_FOLDER     = "visages"

# === CHARGEMENT DES VISAGES AUTORISES (inclus dans le depot Git) ===
known_face_encodings = []
known_face_names = []


def load_known_faces():
    if not os.path.isdir(FACES_FOLDER):
        print(f"Attention : dossier '{FACES_FOLDER}' absent, aucun visage charge.")
        return
    for filename in os.listdir(FACES_FOLDER):
        if not filename.lower().endswith((".jpg", ".jpeg", ".png")):
            continue
        path = os.path.join(FACES_FOLDER, filename)
        image = face_recognition.load_image_file(path)
        encodings = face_recognition.face_encodings(image)
        if encodings:
            known_face_encodings.append(encodings[0])
            known_face_names.append(os.path.splitext(filename)[0])


load_known_faces()
print(f"{len(known_face_names)} visage(s) charge(s) : {known_face_names}")

# === CONNEXION MQTT VERS EMQX CLOUD ===
mqtt_client = mqtt.Client()
if MQTT_USER:
    mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)
if MQTT_PORT == 8883:
    mqtt_client.tls_set()  # connexion chiffree, requise par EMQX Cloud sur ce port

if MQTT_HOST:
    mqtt_client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)
    mqtt_client.loop_start()
else:
    print("MQTT_HOST non defini : la publication sera ignoree (mode test).")

last_unlock_time = 0.0


def trigger_unlock():
    global last_unlock_time
    now = time.time()
    if now - last_unlock_time < UNLOCK_COOLDOWN:
        return False
    if MQTT_HOST:
        mqtt_client.publish(MQTT_TOPIC, "unlock")
    last_unlock_time = now
    return True


@app.route("/")
def index():
    return CAPTURE_PAGE


@app.route("/recognize", methods=["POST"])
def recognize():
    file = request.files.get("frame")
    if file is None:
        return jsonify({"status": "error", "message": "Aucune image recue"}), 400

    image = Image.open(file.stream).convert("RGB")
    rgb_frame = np.array(image)

    face_locations = face_recognition.face_locations(rgb_frame)
    face_encodings = face_recognition.face_encodings(rgb_frame, face_locations)

    if not face_encodings:
        return jsonify({"status": "no_face"})

    for face_encoding in face_encodings:
        if not known_face_encodings:
            continue
        matches = face_recognition.compare_faces(
            known_face_encodings, face_encoding, tolerance=TOLERANCE
        )
        face_distances = face_recognition.face_distance(known_face_encodings, face_encoding)
        best_match_index = int(np.argmin(face_distances))

        if matches[best_match_index]:
            name = known_face_names[best_match_index]
            unlocked = trigger_unlock()
            return jsonify({
                "status": "unlocked" if unlocked else "recognized_but_in_cooldown",
                "name": name,
            })

    return jsonify({"status": "unknown_face"})


CAPTURE_PAGE = """
<!DOCTYPE html>
<html>
<head>
  <title>Serrure faciale</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body style="text-align:center; font-family: sans-serif; padding: 10px;">
  <h2>Serrure faciale</h2>
  <video id="video" autoplay playsinline style="width:100%; max-width:400px; border-radius:8px;"></video>
  <canvas id="canvas" width="400" height="300" style="display:none;"></canvas>
  <p id="status">Initialisation de la camera...</p>
  <script>
    const video = document.getElementById('video');
    const canvas = document.getElementById('canvas');
    const statusEl = document.getElementById('status');

    navigator.mediaDevices.getUserMedia({ video: { facingMode: "user" } })
      .then(stream => {
        video.srcObject = stream;
        statusEl.textContent = "Camera active. Surveillance en cours...";
      })
      .catch(err => { statusEl.textContent = "Erreur camera : " + err; });

    setInterval(() => {
      const ctx = canvas.getContext('2d');
      ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
      canvas.toBlob(blob => {
        const formData = new FormData();
        formData.append('frame', blob, 'frame.jpg');
        fetch('/recognize', { method: 'POST', body: formData })
          .then(res => res.json())
          .then(data => { statusEl.textContent = JSON.stringify(data); })
          .catch(err => { statusEl.textContent = "Erreur reseau : " + err; });
      }, 'image/jpeg', 0.7);
    }, 2000);
  </script>
</body>
</html>
"""

if __name__ == "__main__":
    port = int(os.environ.get("PORT", 8080))
    app.run(host="0.0.0.0", port=port)
