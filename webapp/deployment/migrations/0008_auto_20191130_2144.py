# Generated by Django 2.2.6 on 2019-11-30 21:44

import datetime
from django.db import migrations, models
from django.utils.timezone import utc


class Migration(migrations.Migration):

    dependencies = [
        ('deployment', '0007_auto_20191130_2144'),
    ]

    operations = [
        migrations.AlterField(
            model_name='deployment',
            name='deployment_date',
            field=models.DateTimeField(default=datetime.datetime(2019, 11, 30, 21, 44, 39, 570260, tzinfo=utc), verbose_name='date deployed'),
        ),
    ]
