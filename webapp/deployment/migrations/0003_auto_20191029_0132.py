# Generated by Django 2.2.6 on 2019-10-29 01:32

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ('deployment', '0002_deployment_deployment_id'),
    ]

    operations = [
        migrations.AddField(
            model_name='deployment',
            name='flow_duration',
            field=models.IntegerField(default=0),
        ),
        migrations.AddField(
            model_name='deployment',
            name='flow_volume',
            field=models.IntegerField(default=0),
        ),
        migrations.AddField(
            model_name='deployment',
            name='pump_wait',
            field=models.IntegerField(default=0),
        ),
    ]
