# Video Segmenter - HLS

Outil de segmentation vidéo pour streaming HLS (HTTP Live Streaming).

## Description

Ce programme découpe une vidéo en segments MPEG-TS et génère une playlist M3U8 compatible avec le standard HLS d'Apple.

## Installation

### macOS (Homebrew)

```bash
# Installer FFmpeg avec les bibliothèques de développement
brew install ffmpeg

# Compiler le programme
make
```

### Linux (Ubuntu/Debian)

```bash
# Installer les dépendances FFmpeg
sudo apt-get update
sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev

# Compiler le programme
make
```

## Utilisation

### Syntaxe

```bash
./segment <input_file> <output_dir> <index_file> <base_name> <extension> <segment_duration> [max_segments]
```

### Paramètres

- `input_file` : Fichier vidéo source (mp4, mkv, avi, etc.)
- `output_dir` : Répertoire de sortie pour les segments
- `index_file` : Nom du fichier playlist M3U8
- `base_name` : Préfixe des fichiers segments
- `extension` : Extension des segments (généralement `.ts`)
- `segment_duration` : Durée de chaque segment en secondes
- `max_segments` : (optionnel) Nombre max de segments dans la playlist (0 = illimité)

### Exemples

**Segmentation basique (10 secondes par segment)**
```bash
./segment video.mp4 ./output output.m3u8 segment .ts 10
```

**Streaming en direct (fenêtre glissante de 6 segments)**
```bash
./segment stream.mp4 ./live live.m3u8 chunk .ts 10 6
```

**Segments courts pour low-latency**
```bash
./segment video.mp4 ./output playlist.m3u8 seg .ts 2
```

## Structure de sortie

Après exécution, vous obtiendrez :

```
/tmp/videos/                 Dossier surveillé (déposez vos MP4 ici)
├── processing/          Vidéos en cours de traitement
├── done/                Vidéos traitées avec succès
└── error/               Vidéos en erreur

/var/www/html/streams/     Dossier de sortie
├── nom_video/
├── segment-1.ts
├── segment-2.ts
├── ...
├── nom_video.m3u8
└── info.txt
```

Le fichier `output.m3u8` référence tous les segments et peut être lu par n'importe quel lecteur HLS.

## Lecture des segments

### Avec FFplay
```bash
ffplay output/output.m3u8
```
## Nettoyage

```bash
# Supprimer les fichiers compilés
make clean

# Supprimer les segments générés
rm -rf output/
```