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


def index(request):
    deployment_list =  Deployment.objects.order_by('has_data', 'pk')
    context = {'deployment_list': deployment_list}
    template = loader.get_template('deployment/index.html')
    return HttpResponse(template.render(context, request))


def detail(request, deployment_id):
    try:
        deployment = Deployment.objects.get(pk=deployment_id)
        response = HttpResponse()
        response['depth'] = deployment.depth
        response['pump_wait'] = deployment.pump_wait
        response['flow_volume'] = deployment.flow_volume
        response['flow_duration'] = deployment.flow_duration
    except Device.DoesNotExist:
        raise Http404("Deployment does not exist")
    return render(request, 'deployment/detail.html', {"device": device})


def set_depth(request, uid):
    if request.method == "POST":
        deployment = get_object_or_404(Deployment, eDNA_UID = uid)
        deployment.depth = int(request.POST['depth'])
        deployment.pump_wait = int(request.POST['pump_wait'])
        deployment.flow_volume = int(request.POST['flow_volume'])
        deployment.flow_duration = int(request.POST['flow_duration'])
        deployment.save()
        return HttpResponseRedirect(reverse('index', args=()))
    elif request.method == "GET":
        deployment = get_object_or_404(Deployment, pk = int(uid))
        if deployment.has_data:
            file_name = "{}.txt".format(deployment.eDNA_UID)
            parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            file_dir = os.path.join(parent_dir, 'eDNA', 'data', file_name)
            f_open = open(file_dir, 'rb')
            response = FileResponse(f_open, as_attachment=True)
            return response #response

def get_depth(request, uid):
    if request.method == "GET":
        deployment = get_object_or_404(Deployment, eDNA_UID = uid)
        data = {}
        data['depth'] = deployment.depth
        data['pump_wait'] = deployment.pump_wait
        data['flow_volume'] = deployment.flow_volume
        data['flow_duration'] = deployment.flow_duration
        response = JsonResponse(data)
        return response


def delete_deployment(request, deployment_id):
    if request.method == "POST":
        deployment = get_object_or_404(Deployment, pk=deployment_id)
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
                final_file_name = "{}.txt".format(deployment.eDNA_UID)
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
