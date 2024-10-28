# IoT Room Monitoring and Occupancy Prediction


This project provides a system to monitor room occupancy and predict future room usage trends based on historical data. 
It utilizes IoT sensors, cloud-based processing, and predictive modeling to analyze and forecast the number of people entering a monitored room.

## Project Overview

The project uses light sensors and an IoT device to monitor foot traffic through a room's entrance. 
The system captures data as individuals pass through the entrance and sends it to an ElasticSearch database for storage and analysis. 
With this data, a predictive model (built using Facebook Prophet) is trained to forecast future room occupancy trends. 
The predictions are visualized on our IoT platform to help monitor and optimize room usage.

## System Architecture
The project comprises the following components:

- **IoT Device with Light Sensors:** Two light sensors positioned at the entrance of the monitored room. By tracking sensor activations, we estimate the count of people entering or exiting.

- **Data Transmission to ElasticSearch:** The IoT device periodically sends room occupancy data to ElasticSearch for storage and initial analysis.

- **Function-as-a-Service (FaaS) with OpenWhisk:** Using OpenWhisk on our school’s IoT platform, we analyze data for trends, cleaning, and initial data transformations.

- **Predictive Modeling with Facebook Prophet:** We train a predictive model on historical occupancy data to forecast future room usage.

- **Google Cloud VM for Prediction Execution:** A virtual machine on Google Cloud is configured with a Cronjob to periodically run the trained model, generate predictions, and send results back to the IoT platform for visualization.


## Project Reports
For a more detailed look at our work, check out our reports in project_reports folder.
