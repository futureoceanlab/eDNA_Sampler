import matplotlib.pyplot as plt
import csv
import os

# function to create plots of: pressure, temperature, flow volume and flowrate
# as a functoin of time. It is to give a quick feedback to the user
# who just retreived the data
#
# fileName = name of the .csv file that contains the data
def createPlot(fileName):
	parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
	new_file = os.path.join(parent_dir, 'eDNA', 'data', fileName)

	time = []
	pressure = []
	temperature = []
	clicks = []
	flowrate = []
	with open(new_file, 'r') as file:
		plots = csv.reader(file, delimiter=',')
		next(plots)
		for row in plots:
			#ignores first row
			time.append(row[0])
			pressure.append(row[1])
			temperature.append(row[2])
			clicks.append(row[3])
			flowrate.append(row[4])
		fig, ax = plt.subplots(2,2)
		ax[0,0].plot(time, pressure)
		ax[0,0].set_title("Pressure")
		ax[0,0].get_xaxis().set_visible(False)
		ax[0,0].get_yaxis().set_visible(False)
		ax[0,1].plot(time, temperature)
		ax[0,1].set_title("Temperature")
		ax[0,1].get_xaxis().set_visible(False)
		ax[0,1].get_yaxis().set_visible(False)
		ax[1,0].plot(time, clicks)
		ax[1,0].set_title("Clicks")
		ax[1,0].get_xaxis().set_visible(False)
		ax[1,0].get_yaxis().set_visible(False)
		ax[1,1].plot(time, flowrate)
		ax[1,1].set_title("Flowrate")
		ax[1,1].get_xaxis().set_visible(False)
		ax[1,1].get_yaxis().set_visible(False)
		# save to the webapp/eDNA/static/plots directory
		plt.savefig(os.path.join(parent_dir, 'eDNA', 'static', 'plots', fileName.split(".")[0]+".png"))


