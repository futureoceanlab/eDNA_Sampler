# Generated by Django 2.2.6 on 2019-12-01 18:12

import datetime
from django.db import migrations, models
from django.utils.timezone import utc


class Migration(migrations.Migration):

    dependencies = [
        ('deployment', '0008_auto_20191130_2144'),
    ]

    operations = [
        migrations.AlterField(
            model_name='deployment',
            name='deployment_date',
            field=models.DateTimeField(default=datetime.datetime(2019, 12, 1, 18, 12, 16, 225979, tzinfo=utc), verbose_name='date deployed'),
        ),
        migrations.AlterField(
            model_name='deployment',
            name='flow_volume',
            field=models.FloatField(default=0),
        ),
        migrations.AlterField(
            model_name='deployment',
            name='min_flow_rate',
            field=models.FloatField(default=0),
        ),
    ]