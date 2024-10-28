from minio import Minio
import pickle
# from elasticsearch6 import Elasticsearch
from elasticsearch import Elasticsearch
from datetime import datetime, timedelta
from scipy.stats import norm
import statsmodels.api as sm
from statsmodels.tsa.ar_model import AutoReg
# from statsmodels.iolib.smpickle import save_pickle, load_pickle
import numpy as np
import pytz
import time
import requests

# Data generation
import json
import pandas as pd
from datetime import datetime

import paho.mqtt.client as mqtt

iot_header = {
    "Authorization": "Bearer ourBearerToken"
}


def main():

    minio_client = Minio("caps-platform.live:9900",
                         access_key="group2",
                         secret_key="74Sbrp71xJ",
                         secure=False)

    # Specify the bucket and object name
    bucket_name = 'group2-iot'
    object_name = 'model2.sav'

    # Get the object from the MinIO server

    response = minio_client.get_object(bucket_name, object_name)
    m = pickle.loads(response.data)

    # Do the predictions
    germany_timezone = pytz.timezone('Europe/Berlin')

    current_time = datetime.now() + timedelta(minutes=15)

    end_index = current_time

    future = pd.date_range(start=current_time, end=end_index, freq='5min')

    future_df = pd.DataFrame({'ds': future})
    future_df['subdaily_mask'] = np.where(
        (future_df['ds'].dt.hour >= 9) & (future_df['ds'].dt.hour < 18), 1, 0)
    future_df['weekend_regressor'] = np.where(
        future_df['ds'].dt.dayofweek >= 5, 1, 0)

    forecast = m.predict(future_df)
    pd.set_option('display.max_rows', None)

    # get only the important parts
    forecast_with_fields = forecast[['ds', 'yhat', 'yhat_lower', 'yhat_upper']]
    # start putting the predictions in the dictionary

    only_predictions = forecast_with_fields
    only_predictions['yhat'] = np.where(
        only_predictions['yhat'] < 6, 0, only_predictions['yhat'])
    print(only_predictions)
    # connect to iot platform
    client = mqtt.Client()
    client.username_pw_set("JWT", "ourToken")

    client.connect("mqtt.caps-platform.live", 1883, 60)

    germany_timezone = pytz.timezone('Europe/Berlin')
    final_timestamp = 0
    only_predictions.set_index('ds', inplace=True)
    for index, row in only_predictions.iterrows():
        value = round(row['yhat'])
        predictionVal = int(value)
        # Perform your desired operations with the value
        # For example:
        print("index is: ", index)
        print("value is: ", value)
        localized_index = index
        timestampVal = int(localized_index.timestamp()) * 1000

        data = {
            "sensors": [
                {
                    "name": "prediction_new",
                    "values": [
                            {
                                "timestamp": timestampVal,
                                "prediction_count": predictionVal
                            }
                    ]
                }
            ]
        }
        y = json.dumps(data)
        msg_id = client.publish(topic="31/78/data", payload=y)
        print("msg id")
        print(msg_id)
        final_value = value
    client.disconnect()

    url = f"http://caps-platform.live:3000/api/users/31/config/device/update?type=device&deviceId=78&prediction={final_value}"

    response = requests.get(url, headers=iot_header)

    if response.status_code == 200:
        print("Request successful!")
        print(response)  # Assuming the response is in JSON format
    else:
        print(f"Request failed with status code {response.status_code}")


main()
