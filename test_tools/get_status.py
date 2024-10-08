import websocket
import _thread
import time

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
            # ws.send("{\"command\" : 0}")
            # time.sleep(2)
            # ws.send("{\"command\" : 0}")
            time.sleep(10)
            print(i)
        ws.close()
    _thread.start_new_thread(run, ())

if __name__ == "__main__":
    ws = websocket.WebSocketApp("ws://192.168.31.150:8080/2",
                              on_message = on_message,
                              on_error = on_error,
                              on_close = on_close)
    ws.on_open = on_open
    ws.run_forever()
