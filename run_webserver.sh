#/usr/bin/env bash

cd /home/pi/eDNA_Sampler/webapp
source env/bin/activate
python3 manage.py runserver 0.0.0.0:5000
