from datetime import datetime
from elasticsearch import Elasticsearch
import pandas as pd
import time
from scipy.stats import norm
import statsmodels.api as sm
import logging
from minio import Minio
from opentelemetry import trace
from opentelemetry.exporter.jaeger.proto.grpc import JaegerExporter
from opentelemetry.sdk.resources import SERVICE_NAME, Resource
from opentelemetry.sdk.trace import TracerProvider
from opentelemetry.sdk.trace.export import BatchSpanProcessor
import datetime
from prophet import Prophet
import numpy as np
from statsmodels.tsa.ar_model import AutoReg
import pickle
import io
import pytz

logging.basicConfig(level=logging.INFO)


trace.set_tracer_provider(
    TracerProvider(
        resource=Resource.create({SERVICE_NAME: "ow-function-model-02"})
    )
)

jaeger_exporter = JaegerExporter(
    collector_endpoint='138.246.238.62:14250',
    insecure=True
)

trace.get_tracer_provider().add_span_processor(
    BatchSpanProcessor(jaeger_exporter)
)

tracer = trace.get_tracer(__name__)

minioAddress = "iot-minio-exposed.default.svc.cluster.local:9900"

# Connecting to MinIO
minioClient = Minio(minioAddress,
                    access_key="group2",
                    secret_key="74Sbrp71xJ",
                    secure=False)

logging.info(f"Minioclient: {minioClient}")


def main(request):
    elastic_search_host = "http://group2:74Sbrp71xJ@iot-elasticsearch.default.svc.cluster.local:9200"
    client = Elasticsearch(
        elastic_search_host,
    )

    cutoff_date_start = datetime.datetime(2023, 6, 12, 0, 0, 0)
    cutoff_date_end = datetime.datetime(2023, 6, 25, 23, 59, 59)

    es_query = {
        "query": {
            "bool": {
                "must": [
                    {"match_all": {}},
                    {"range": {"timestamp": {
                        "gte": cutoff_date_start.isoformat(), "lte": cutoff_date_end.isoformat()}}}
                ]
            }
        }
    }
    # Please attach group-id to spans, e.g. getesdata-group2
    with tracer.start_as_current_span("getesdata-group2"):
        result = client.search(
            index="31_78_243", body=es_query, size=10000, scroll="5m")
        with tracer.start_as_current_span("parseesdata-group2"):
            df = parse_data(result)
            with tracer.start_as_current_span("trainmodel-group2"):
                model = test_model(df)
                with tracer.start_as_current_span("savemodel-group2"):
                    bytes_file = pickle.dumps(model)
                    # Storing Model
                    result = minioClient.put_object(
                        bucket_name="group2-iot",
                        object_name="model2.sav",
                        data=io.BytesIO(bytes_file),
                        length=len(bytes_file)
                    )
                    print(
                        "created {0} object; etag: {1}, version-id: {2}".format(
                            result.object_name, result.etag, result.version_id,
                        )
                    )

    return {"result": "done"}


def test_model(df):
    df['ds'] = pd.to_datetime(df['ds'])
    mask = (df['ds'].dt.hour >= 9) & (df['ds'].dt.hour <= 19)

    # describe regressors
    df['weekend_regressor'] = np.where(df['ds'].dt.dayofweek >= 5, 1, 0)

    # Assign values of 1 for data points within the time range and 0 for data points outside
    df['subdaily_mask'] = np.where(mask, 1, 0)

    m = Prophet(changepoint_prior_scale=8,
                daily_seasonality=400, weekly_seasonality=80)
    # add regressors
    m.add_regressor('subdaily_mask')
    m.add_regressor('weekend_regressor')
    m.fit(df)

    return m


def parse_data(json_data):
    time_values = []
    sensor_values = []

    data_values = json_data["hits"]["hits"]
    german_timezone = pytz.timezone('Europe/Berlin')

    for i in range(0, len(data_values)):
        timestamp = data_values[i]["_source"]["timestamp"]
        data_value = (data_values[i]["_source"]["value"]["count"])

        # Convert the Unix epoch to a datetime object
        utc_time = datetime.datetime.utcfromtimestamp(timestamp/1000)

        # Set the time zone of the datetime object to Germany
        german_time = utc_time.replace(
            tzinfo=pytz.utc).astimezone(german_timezone)

        # Format the German time as a string
        timestamp_converted = german_time.strftime('%Y-%m-%d %H:%M:%S')
        # Convert the timestamp to a datetime object in Germany timezone

        time_values.append(timestamp_converted)
        sensor_values.append(data_value)

    df = pd.DataFrame(
        {
            "ds": time_values,
            "y": sensor_values
        }
    )
    sorted_df = df.sort_values(by='ds')
    return sorted_df
