# Variables
SCRIPTS=install.sh video_processor.sh
BIN=./usr/local/bin/video_processor.sh
LOG=./var/log/video_processor.log
VIDEO_TMP=./tmp/videos
#TEST_VIDEO=ma_video.mp4

#SCRIPTS=install.sh video_processor.sh
#BIN=${HOME}/Works/video_orchestrator/src/main/resources/usr/local/bin/video_processor.sh
#LOG=${HOME}/Works/video_orchestrator/src/main/resources/var/log/video_processor.log
#VIDEO_TMP=${HOME}/Works/video_orchestrator/src/main/resources/tmp/videos
TEST_VIDEO=video.mp4

.PHONY: help chmod install logs test watch copy cleanup cron

help:
	@echo "Cibles disponibles :"
	@echo "  make chmod     -> rendre les scripts exécutables"
	@echo "  make install   -> installer (sudo requis)"
	@echo "  make logs      -> voir les logs en direct"
	@echo "  make test      -> exécuter un traitement manuel"
	@echo "  make watch     -> lancer le mode surveillance"
	@echo "  make copy      -> copier une vidéo de test"
	@echo "  make cleanup   -> nettoyer les fichiers > 7 jours"
	@echo "  make cron      -> afficher les tâches cron"

chmod:
	chmod +x $(SCRIPTS)

install:
	sudo ./install.sh

logs:
	tail -f $(LOG)

test:
	$(BIN)

watch:
	$(BIN) watch

copy:
	cp $(TEST_VIDEO) $(VIDEO_TMP)/

cleanup:
	$(BIN) cleanup 7

cron:
	crontab -l