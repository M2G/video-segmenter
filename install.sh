#!/bin/bash

###############################################
# Script d'installation du Video Processor
###############################################

set -e

echo "╔════════════════════════════════════════╗"
echo "║   Installation Video Processor         ║"
echo "╚════════════════════════════════════════╝"
echo ""

# Vérifie les droits root
if [ "$EUID" -ne 0 ]; then 
    echo "Ce script doit être exécuté en root (sudo)"
    exit 1
fi

# 1. Compilation du segmenteur
echo "Compilation du video_segmenter..."
if [ -f "video_segmenter.c" ]; then
    gcc -Wall -Wextra -O2 $(pkg-config --libs libavformat libavcodec libavutil) \
        -o video_segmenter video_segmenter.c \
        $(pkg-config --cflags libavformat libavcodec libavutil)
    echo "Compilation réussie"
else
    echo "Fichier video_segmenter.c introuvable"
    exit 1
fi

# 2. Installation des binaires
echo "Installation des binaires..."
#install -m 755 video_segmenter /usr/local/bin/
#install -m 755 video_processor.sh /usr/local/bin/
#echo "Binaires installés dans /usr/local/bin/"
install -m 755 video_segmenter ./usr/local/bin/
install -m 755 video_processor.sh ./usr/local/bin/
echo "Binaires installés dans ./usr/local/bin/"

# 3. Création des dossiers
echo "Création des dossiers..."
#mkdir -p /tmp/videos/{processing,done,error}
#mkdir -p /var/www/html/streams
#mkdir -p /var/log
#touch /var/log/video_processor.log
mkdir -p ./tmp/videos/{processing,done,error}
mkdir -p ./var/www/html/streams
mkdir -p ./var/log
touch ./var/log/video_processor.log
echo "Dossiers créés"

# 4. Configuration des permissions
echo "Configuration des permissions..."
if id "www-data" &>/dev/null; then
#    chown -R www-data:www-data /var/www/html/streams
#    chown www-data:www-data /var/log/video_processor.log
    chown -R www-data:www-data ./var/www/html/streams
    chown www-data:www-data ./var/log/video_processor.log
    echo "Permissions configurées (utilisateur www-data)"
else
    echo "Utilisateur www-data introuvable, permissions non modifiées"
fi

# 5. Configuration du cron
echo "Configuration du cron..."
#CRON_LINE="*/5 * * * * /usr/local/bin/video_processor.sh >> /var/log/video_processor_cron.log 2>&1"
#CLEANUP_LINE="0 3 * * 0 /usr/local/bin/video_processor.sh cleanup 7"
CRON_LINE="*/5 * * * * ./usr/local/bin/video_processor.sh >> ./var/log/video_processor_cron.log 2>&1"
CLEANUP_LINE="0 3 * * 0 ./usr/local/bin/video_processor.sh cleanup 7"

# Ajoute au cron si pas déjà présent
(crontab -l 2>/dev/null | grep -v video_processor.sh; echo "$CRON_LINE"; echo "$CLEANUP_LINE") | crontab -

echo "Tâches cron configurées (vérification toutes les 5 min)"

# 6. Test de l'installation
echo ""
echo "Test de l'installation..."
#if /usr/local/bin/video_segmenter 2>&1 | grep -q "Usage"; then
if ./usr/local/bin/video_segmenter 2>&1 | grep -q "Usage"; then
    echo "video_segmenter fonctionne"
else
    echo "video_segmenter ne fonctionne pas correctement"
fi

#if [ -x /usr/local/bin/video_processor.sh ]; then
if [ -x ./usr/local/bin/video_processor.sh ]; then
    echo "video_processor.sh est exécutable"
else
    echo "video_processor.sh n'est pas exécutable"
fi

# 7. Résumé
echo ""
echo "╔════════════════════════════════════════╗"
echo "║   Installation terminée !              ║"
echo "╚════════════════════════════════════════╝"
echo ""
echo "Configuration:"
echo "   Dossier surveillé:  /tmp/videos"
echo "   Dossier de sortie:  /var/www/html/streams"
echo "   Logs:               /var/log/video_processor.log"
echo "   Fréquence:          Toutes les 5 minutes"
echo ""
echo "Commandes utiles:"
echo "   Test manuel:        /usr/local/bin/video_processor.sh"
echo "   Mode surveillance:  /usr/local/bin/video_processor.sh watch"
echo "   Voir les logs:      tail -f /var/log/video_processor.log"
echo "   Voir le cron:       crontab -l"
echo "   Test immédiat:      cp test.mp4 /tmp/videos/"
echo ""
echo "Pour tester maintenant:"
echo "   cp votre_video.mp4 /tmp/videos/"
echo "   /usr/local/bin/video_processor.sh"
echo ""
