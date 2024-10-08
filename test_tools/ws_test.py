import websocket
import _thread
import time

import json

json_data = {
    "command": 1,
    "text": [
        {
            "data": "01A234AB",
            "size": 1,
            "color": [
                0,
                255,
                0
            ]
        },
        {
            "data": "2:30",
            "color": [
                255,
                0,
                0
            ]
        },
        {
            "data": "990000",
            "size": 2,
            "color": [0,255,0]
        }
    ]
}

python_dict = json.loads(json.dumps(json_data))
# python_dict["text"][2]["data"] = "123"

def on_message(ws, message):
    print("Received: %s" % message)

def on_error(ws, error):
    print("Error: %s" % error)

def on_close(ws):
    print("Connection closed")

def on_open(ws):
    def run(*args):
        i = 0
        while 1:
            i = i + 1
            # time.sleep(1)
            # ws.send("{\"command\": 0}")
            python_dict["text"][2]["data"] = '{count}'.format(count = i)
            ws.send(json.dumps(python_dict))

            # ws.send("{\"command\": 1, \"text\": [{\"data\": \"01A234AB\",\"size\": 1,\"color\": [0,255,0]},{\"data\": \"2:30\",\"color\": [255,0,0]},{\"data\": \"990000\",\"size\": 2,\"color\": [0,255,0]}]}")
            time.sleep(0.5)
            print(i)
        ws.close()
    _thread.start_new_thread(run, ())

if __name__ == "__main__":
    ws = websocket.WebSocketApp("ws://192.168.24.119:8080/3",
                              on_message = on_message,
                              on_error = on_error,
                              on_close = on_close)
    ws.on_open = on_open
    ws.run_forever()
