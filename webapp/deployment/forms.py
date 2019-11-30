from django import forms

class DeploymentForm(forms.Form):
    depth = forms.IntegerField(help_text="precision: 1m")
    depth_band = forms.IntegerField()     
    temperature = forms.FloatField(help_text="precision: 0.1C") 
    temp_band = forms.FloatField(help_text="precision: 0.1C")
    wait_pump_start = forms.IntegerField()      
    
    min_flow_rate = forms.IntegerField()                
    wait_pump_end = forms.IntegerField()
    flow_volume = forms.IntegerField()
    
    ticks_per_L = forms.IntegerField(required=True)
