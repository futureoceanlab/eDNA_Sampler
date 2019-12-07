import os
import time

from django.shortcuts import render, get_object_or_404, reverse
from django.http import HttpResponse, HttpResponseRedirect, Http404, FileResponse, JsonResponse
from django.template import loader
from django.views.decorators.csrf import csrf_exempt
from django.utils import timezone
from django.utils.encoding import smart_str
from django.core.exceptions import ObjectDoesNotExist



from .models import Device, Deployment
from.forms import DeploymentForm

def index(request):
    deployment_list =  Deployment.objects.order_by('has_data', 'pk')
    context = {'deployment_list': deployment_list}
    template = loader.get_template('deployment/index.html')
    return HttpResponse(template.render(context, request))

def handle_logs(request):
    parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    file_path = os.path.join(parent_dir, 'eDNA', 'logs')
    logs_list = [f.split('.')[0] for f in os.listdir(file_path) if os.path.isfile(os.path.join(file_path, f))]
    context = {'logs_list': logs_list}
    return render(request, 'deployment/view_logs.html', context)

def detail(request, uid):
    deployment = get_object_or_404(Deployment, eDNA_UID = uid)
    page_data = {"saved": False}

    if request.method == "POST":
        form = DeploymentForm(request.POST)
        if form.is_valid():
            deployment.depth = form.cleaned_data['depth']
            deployment.depth_band = form.cleaned_data['depth_band']
            deployment.temperature = form.cleaned_data['temperature']
            deployment.temp_band = form.cleaned_data['temp_band']
            deployment.wait_pump_start = form.cleaned_data['wait_pump_start']
            deployment.flow_volume = form.cleaned_data['flow_volume']
            deployment.min_flow_rate = form.cleaned_data['min_flow_rate']
            deployment.wait_pump_end = form.cleaned_data['wait_pump_end']
            deployment.ticks_per_L = form.cleaned_data['ticks_per_L']
            deployment.notes = form.cleaned_data['notes']
            deployment.save()
            page_data["saved"] = 1
        else:
            page_data["saved"] = 2

    form = DeploymentForm(initial={
        'depth': deployment.depth,
        'depth_band': deployment.depth_band,
        'temperature': deployment.temperature,
        'temp_band': deployment.temp_band,
        'wait_pump_start': deployment.wait_pump_start,
        'flow_volume': deployment.flow_volume,
        'min_flow_rate': deployment.min_flow_rate,
        'wait_pump_end': deployment.wait_pump_end,
        'notes': deployment.notes,
    })
    if deployment.ticks_per_L > 0:
        form.initial['ticks_per_L'] =  deployment.ticks_per_L

    page_data["form"] = form
    page_data["deployment"] = deployment
    return render(request, 'deployment/detail.html', page_data)

def get_data(request, uid):
    deployment = get_object_or_404(Deployment, eDNA_UID = uid)
    if deployment.has_data:
        file_name = "{}.txt".format(deployment.eDNA_UID)
        parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        file_dir = os.path.join(parent_dir, 'eDNA', 'data', file_name)
        f_open = open(file_dir, 'rb')
        response = FileResponse(f_open, as_attachment=True)
        return response #response

def get_log(request, log_name):
    file_name = "{}.txt".format(log_name)
    parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    file_dir = os.path.join(parent_dir, 'eDNA', 'logs', file_name)
    if (not os.path.exists(file_dir)):
        return Http404("Log file does not exist")
    f_open = open(file_dir, 'rb')
    response = FileResponse(f_open, as_attachment=True)
    return response #response

def get_config(request, uid):
    if request.method == "GET":
        deployment = get_object_or_404(Deployment, eDNA_UID = uid)
        data = {
            'depth': deployment.depth,
            'depth_band': deployment.depth_band,
            'temperature': deployment.temperature,
            'temp_band': deployment.temp_band,
            'wait_pump_start': deployment.wait_pump_start,
            'flow_volume': deployment.flow_volume,
            'min_flowrate': deployment.min_flow_rate,
            'wait_pump_end': deployment.wait_pump_end,
            'ticks_per_L':  deployment.ticks_per_L
        }
        response = JsonResponse(data)
        return response


def delete_deployment(request, uid):
    if request.method == "POST":
        deployment = get_object_or_404(Deployment, eDNA_UID=uid)
        deployment.delete()
        return HttpResponseRedirect(reverse('index', args=()))

def get_datetime(request):
    if request.method == "GET":
        datetime_now = int(time.mktime(timezone.now().timetuple())) # Timezone now defaults to UTC
        data = {"now": datetime_now}
        print(datetime_now)
        return JsonResponse(data)

@csrf_exempt
def upload_deployment_data(request, uid):
    if request.method == "POST":
        deployment = get_object_or_404(Deployment, eDNA_UID=uid)
        if (deployment.has_data == False):
            n_chunks = int(request.headers["Chunks"])
            num_bytes = int(request.headers["Data-Bytes"])
            nth_chunk = int(request.headers["Nth"])
            parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            print(n_chunks)
            print(nth_chunk)
            print(num_bytes)
            
            if (nth_chunk < n_chunks):
            # Accumulate data first
                file_name = "{}_{}.txt".format(deployment.eDNA_UID, nth_chunk)
                new_file = os.path.join(parent_dir, 'eDNA', 'data', file_name)
                with open(new_file, 'wb+') as dest:
                    dest.write(request.body[0:num_bytes])
            elif (nth_chunk == n_chunks):
                for i in range(1, n_chunks):
                    file_name = "{}_{}.txt".format(deployment.eDNA_UID, i)
                    file_path = os.path.join(parent_dir, 'eDNA', 'data', file_name)
                    if not os.path.exists(file_path):
                        print("file not exists")
                        raise Http404("Missing intermediate files, send again")
                final_file_name = "{}.csv".format(deployment.eDNA_UID)
                new_file = os.path.join(parent_dir, 'eDNA', 'data', final_file_name)
                with open(new_file, 'wb+') as dest:
                    for i in range(1, n_chunks):
                        file_name = "{}_{}.txt".format(deployment.eDNA_UID, i)
                        file_path = os.path.join(parent_dir, 'eDNA', 'data', file_name)
                        with open(file_path, 'rb') as temp_f:
                            dest.write(temp_f.read())
                        os.remove(file_path)
                    dest.write(request.body[0:num_bytes])
                print("Done")
                deployment.has_data = True
                deployment.is_new = False
                deployment.save()
            else:
                raise Http404("Unexpected nth chunk")

        return HttpResponse(status=200)
    else:
        raise Http404("Invalid Post requst to deployment")

@csrf_exempt
def upload_log(request, uid):
    if request.method == "POST":
        n_chunks = int(request.headers["Chunks"])
        num_bytes = int(request.headers["Data-Bytes"])
        nth_chunk = int(request.headers["Nth"])
        parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        print(n_chunks)
        print(nth_chunk)
        print(num_bytes)
            
        if (nth_chunk < n_chunks):
        # Accumulate data first
            file_name = "temp_log_{}.txt".format(nth_chunk)
            new_file = os.path.join(parent_dir, 'eDNA', 'logs', file_name)
            with open(new_file, 'wb+') as dest:
                dest.write(request.body[0:num_bytes])
        elif (nth_chunk == n_chunks):
            # Check that all the intermediate files exist
            for i in range(1, n_chunks):
                file_name = "temp_log_{}.txt".format(i)
                file_path = os.path.join(parent_dir, 'eDNA', 'logs', file_name)
                if not os.path.exists(file_path):
                    print("file not exists")
                    raise Http404("Missing intermediate files, send again")
            # create the final file
            datetime_now = time.strftime("%Y%m%d%H%M")
            final_file_name = "log_{}_{}.txt".format(datetime_now, uid)
            new_file = os.path.join(parent_dir, 'eDNA', 'logs', final_file_name)
            # read from all the intermediate files upon which they are erased
            with open(new_file, 'wb+') as dest:
                for i in range(1, n_chunks):
                    file_name = "temp_log_{}.txt".format(i)
                    file_path = os.path.join(parent_dir, 'eDNA', 'logs', file_name)
                    with open(file_path, 'rb') as temp_f:
                        dest.write(temp_f.read())
                    os.remove(file_path)
                dest.write(request.body[0:num_bytes])
            print("Done")
        else:
            raise Http404("Unexpected nth chunk")
        return HttpResponse(status=200)

    else:
        raise Http404("Invalid Post requst to deployment")

@csrf_exempt
def create_deployment(request, device_id):
    if request.method == "POST":
        device, device_created = Device.objects.get_or_create(
            device_id = device_id
        )
        if device_created:
            device.save()
        eDNA_UID = request.body[0:8].decode("utf-8") 
        print(eDNA_UID)
        deployment, dep_created = Deployment.objects.get_or_create(
            device=device,
            eDNA_UID=eDNA_UID
        )
        if dep_created:
            deployment.save()
        return HttpResponse(status=200)

def check_deployment(request, device_id):
    if request.method == "GET":
        data = {}
        device, device_created = Device.objects.get_or_create(device_id = device_id)
        if device_created:
            data['status'] = 0
            device.save()
        else:
            deployment = Deployment.objects.filter(device=device, is_new=True).order_by('deployment_date').first()
            if deployment:
                data['status'] = 1
                data['eDNA_UID'] = deployment.eDNA_UID
            else:
                data['status'] = 0
        response = JsonResponse(data)
        return response
