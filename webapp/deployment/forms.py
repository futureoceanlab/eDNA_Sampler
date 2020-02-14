##
# This is the form that the user fills in the configuration page. 
#
#
# Date: January 2020
# Author: Junsu Jang
# email: junsuj@mit.edu

from django import forms


class DeploymentForm(forms.Form):
    depth = forms.IntegerField(help_text="precision: 1m")
    depth_band = forms.IntegerField()     
    temperature = forms.FloatField(help_text="precision: 0.1C") 
    temp_band = forms.FloatField(help_text="precision: 0.1C")
    wait_pump_start = forms.IntegerField()      
    
    min_flow_rate = forms.FloatField()                
    wait_pump_end = forms.IntegerField()
    flow_volume = forms.FloatField()
    
    ticks_per_L = forms.IntegerField(required=True)

    notes = forms.CharField(widget=forms.Textarea(attrs={"rows":5, "cols":40}))

    # check that each input value is valid
    def clean(self):
        data = self.cleaned_data
        filled_depth = not(data['depth'] == 0 and data['depth_band'] == 0)
        filled_temp = not(data['temperature'] == -273.15 and data['temp_band'] == 0)
        filled_wait_start = data['wait_pump_start'] != 0
        filled_start = filled_depth or filled_temp or filled_wait_start
        if not filled_start:
            raise forms.ValidationError("Please specify starting at least \
                one condition. Depth and temperature need to be specified \
                    together with the band")

        filled_fr = data['min_flow_rate'] != 0
        filled_wait_end = data['wait_pump_end'] != 0
        filled_vol = data['flow_volume'] != 0
        filled_end = filled_fr or filled_wait_end or filled_vol
        if not filled_end:
            raise forms.ValidationError("Please specify at least one \
                pump ending condition.")
        
        return data