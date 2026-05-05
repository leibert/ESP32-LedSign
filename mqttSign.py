"""mqttSign.py

Control an RGB LED matrix panel via MQTT messages. Designed to run on a
Raspberry Pi with the `rpi-rgb-led-matrix` library installed. The
script subscribes to several topics and updates the display accordingly.

Before running, create a `.env` file with appropriate MQTT settings or
export the variables directly in the environment. A sample file is
included as `env.sample` in the project root.

Usage: sudo python mqttSign.py --led-chain=3 --led-rows=16
"""

# bring configuration values from environment (dotenv is optional)
import os
from datetime import datetime, timedelta
import sys

# allow imports from the rpi-rgb-led-matrix python bindings
sys.path.append('/home/pi/rpi-rgb-led-matrix/bindings/python')

from cgitb import text
from samples.samplebase import SampleBase
from rgbmatrix import graphics

# MQTT client library
import paho.mqtt.client as mqtt
import time

# load environment variables from a .env file if present
try:
    from dotenv import load_dotenv
    load_dotenv()
except ImportError:
    # dotenv is optional; can use plain os.environ
    pass

# mqtt configuration comes from env vars; defaults are for a container
mqttBroker = os.getenv("MQTT_BROKER", "mqtt")
mqttUsername = os.getenv("MQTT_USERNAME", "mqtt")
mqttPassword = os.getenv("MQTT_PASSWORD", "")

client = mqtt.Client("LED Sign")
if mqttUsername or mqttPassword:
    client.username_pw_set(mqttUsername, password=mqttPassword)



#led sign setup
font46 = graphics.Font()
font46.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/4x6.bdf")


font57 = graphics.Font()
font57.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/5x7.bdf")
font58 = graphics.Font()
font58.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/5x8.bdf")

font69 = graphics.Font()
font69.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/6x9.bdf")

font610 = graphics.Font()
font610.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/6x10.bdf")

font613B = graphics.Font()
font613B.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/6x13B.bdf")

font714 = graphics.Font()
font714.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/7x14.bdf")

font714B = graphics.Font()
font714B.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/7x14B.bdf")


fontHR12 = graphics.Font()
fontHR12.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/helvR12.bdf")

fontclR612 = graphics.Font()
fontclR612.LoadFont("/home/pi/rpi-rgb-led-matrix/fonts/clR6x12.bdf")

textColor = graphics.Color(0, 0, 255)


nextEvent = {}
# nextEvent["start"]=datetime.fromtimestamp(float("1676175535.0"))
# nextEvent["start"]=datetime.now()+timedelta(minutes=0,seconds=15)
# nextEvent["subject"]="Testing"
# nextEvent["organizer"]="Colin McCarthy"
# nextEvent["attendees"]="Bob, Sam, Jill"
# nextEvent["isExternal"]="False"
nextEvent["start"]=None
nextEvent["subject"]=None
nextEvent["organizer"]=None
nextEvent["attendees"]=None
nextEvent["isExternal"]=None

def on_message(client, userdata, msg):
    """Callback invoked when an MQTT message is received.

    Depending on the topic the message updates attributes on the running
    sign instance or adjusts global state such as color or the next-event
    dictionary. Topics are under `ledSign/` and `nextEvent/` prefixes.
    """
    global textColor

    # log incoming messages for debugging
    print(f"Received `{msg.payload.decode()}` from `{msg.topic}` topic")
    topic = msg.topic
    print(topic)
    if topic == "ledSign/line1":
        run_text.line1 = msg.payload.decode()
        print("updated line1 to: " + run_text.line1)

    elif topic == "ledSign/line2":
        run_text.line2 = msg.payload.decode()
        print("updated line2 to: "+ line2)

    elif topic == "ledSign/line3":
        run_text.line2 = msg.payload.decode()
        print("updated line3 to: "+ line3)

    elif topic == "ledSign/color":
        color = msg.payload.decode()
        print("updated color to: "+ line2)
        a,b,c = color.split(',')
        print(a)
        print(b)
        print(c)
        textColor = graphics.Color(a, b, c)
    elif topic == "ledSign/mode":
        print("new mode recieved")
        print(msg.payload.decode())
        run_text.newCommand=True
        run_text.mode = msg.payload.decode()

    elif topic == "ledSign/message/type":
        run_text.messageType=msg.payload.decode()
    elif topic == "ledSign/message/sender":
        run_text.messageSender=msg.payload.decode()
    elif topic == "ledSign/message/text":
        run_text.messageText=msg.payload.decode()
    elif topic == "ledSign/EN":
        if (msg.payload.decode()=="OFF"):
            run_text.mode="OFF"
            run_text.displayEnabled=False
            client.publish("ledSign/mode","OFF")
        else:
            run_text.displayEnabled=True
            run_text.mode="bigClock"
            client.publish("ledSign/mode","bigClock")
    elif topic == "nextEvent/timeStamp":
        try:
            print ("SSqDEBUG")
            nextEvent['start']=datetime.fromtimestamp(float(msg.payload.decode())+1)
            print ("SSSDEBUG")
            print (nextEvent['start'])
        except:
            nextEvent['start'] = None
    elif topic == "nextEvent/subject":
       nextEvent['subject']=msg.payload.decode()
    elif topic == "nextEvent/organizer":
       nextEvent['organizer']=msg.payload.decode()
    elif topic == "nextEvent/attendees":
       nextEvent['attendees']=msg.payload.decode()
    elif topic == "nextEvent/isExternal":
       nextEvent['isExternal']=msg.payload.decode()

    



def on_connect(client, userdata, flags, rc):
    """MQTT connection callback.

    Once connected successfully we subscribe to the topics we care about
    so that any disconnections/reconnections will automatically resubscribe.
    """
    print("Connected with result code "+str(rc))

    # subscribe to all sign and event topics
    client.subscribe("ledSign/#")
    client.subscribe("nextEvent/#")




    

class RunSign(SampleBase):
    """Main application class controlling the LED matrix display.

    Inherits from `SampleBase` provided by the rpi-rgb-led-matrix samples.
    Attributes are used to track the current display mode, text lines, and
    scrolling position. The `run` method contains the primary loop that
    updates the display based on the current mode and any commands received
    via MQTT.
    """

    # canvas we draw to before swapping
    offscreen_canvas = None
    scrollCounter = 0

    mode = "bigClock"
    displayEnabled = True
    # mode = "eventCountdown"

    messageType = ""
    messageSender = ""
    messageText = ""

    newCommand = False


    #led sign properties
    line1 = "Test Line 1"
    line1pos=0
    line1len=0
    line2 = "Test Line 2"
    line2pos=0
    line2len=0
    line3 = "Test Line 3"
    line3pos=0
    line3len=0


    def __init__(self, *args, **kwargs):
        super(RunSign, self).__init__(*args, **kwargs)
        self.parser.add_argument("-t", "--text", help="The text to scroll on the RGB LED panel", default="Hello world!")

    def run(self):
        """Entry point for the sign's main loop.

        Called by `SampleBase.process()`, this method initializes the
        offscreen canvas and then enters an endless loop where it updates the
        display every 0.1 seconds according to `self.mode`. Modes include
        clock displays, scrolling text, static lines, and event countdowns.
        """
        self.offscreen_canvas = self.matrix.CreateFrameCanvas()
        print("A2")
        line1_pos = self.offscreen_canvas.width
        line2_pos = self.offscreen_canvas.width
        line3_pos = self.offscreen_canvas.width
        print("A3")
        # my_text = self.args.text

        while True:
            # if datetime.now().second % 5 == 0:
            #     print(self.mode)
            #     print(self.messageType)
            time.sleep(0.1)
            self.offscreen_canvas.Clear()
            # self.offscreen_canvas.Clear()
            self.scrollCounter +=1 

            try:
                if not self.displayEnabled:
                    self.offscreen_canvas.Clear()

                elif self.mode == "clock":
                    # time.sleep(0.15)
                    self.clockLine()
                elif self.mode == "bigClock":
                    self.bigClock()
                elif self.mode == "message":
                    if self.newCommand:
                        print("FLSH")
                        print(self.messageType)
                        print(self.messageSender)
                        self.line1=self.messageType + " || " + self.messageSender
                        self.line2=self.messageText
                        print(self.line1+"/"+self.line2)
                    self.staticLine1(font=font46, color = graphics.Color(0, 0, 255), reset=self.newCommand)
                    self.staticLine2(font=font610, color = graphics.Color(255, 255,0), reset=self.newCommand)

                    
                elif self.mode == "static":
                    self.staticLine1()
                    self.staticLine2()
                    self.staticLine3()
                elif self.mode == "scroll":
                    # self.scrollCounter +=1 
                    self.scrollLine1()
                
                elif self.mode == "eventCountdown":
                    if nextEvent is None or "subject" not in nextEvent:
                        print("no Event")
                        continue
                    if (nextEvent["start"] and nextEvent["start"]>datetime.now()):
                        secondsUntilNextEvent=(nextEvent["start"] - datetime.now()).seconds
                        if secondsUntilNextEvent < 300 and secondsUntilNextEvent > 290:
                            if secondsUntilNextEvent % 2:
                                self.offscreen_canvas.Fill(80,80,80)
                        elif secondsUntilNextEvent < 120 and secondsUntilNextEvent > 110:
                            if secondsUntilNextEvent % 2:
                                self.offscreen_canvas.Fill(80,80,0)
                        elif secondsUntilNextEvent < 60 and secondsUntilNextEvent > 50:
                            if secondsUntilNextEvent % 2:
                                self.offscreen_canvas.Fill(150,0,0)
                        elif secondsUntilNextEvent < 10 and secondsUntilNextEvent > 0:
                            if secondsUntilNextEvent % 2:
                                self.offscreen_canvas.Fill(0,60,0)
                        self.line3="-"+str(int(secondsUntilNextEvent/60))+":"+str(secondsUntilNextEvent%60).zfill(2)
                        self.countDownDisplay(color=graphics.Color(160,160,0))
                    else:
                        secondsSinceNextEvent=(datetime.now()-nextEvent["start"]).seconds
                        if secondsSinceNextEvent < 5:
                            self.offscreen_canvas.Fill(0,100,0)
                        elif secondsSinceNextEvent > 120:
                            self.mode="bigClock"
                        self.line3="+"+str(int(secondsSinceNextEvent/60))+":"+str(secondsSinceNextEvent%60).zfill(2)
                        self.countDownDisplay(font=font610, color=graphics.Color(0,80,0))


                    self.clockLineR()
                    self.line1=nextEvent["subject"]
                    self.staticLine1(font=font610, ypos=7, color = graphics.Color(0, 0, 255),overScroll=16)
                    # self.countDownDisplay()

                    if(secondsUntilNextEvent%60 < 15):
                        self.line2="Org:"+nextEvent["organizer"]
                    else:
                        self.line2=nextEvent["attendees"]
                    
                    self.staticLine2(font=font46, ypos=15, color = graphics.Color(50, 50, 50),overScroll=27)




                    # self.scrollCounter +=1 
                    # self.scrollLine1()


                else:
                    self.offscreen_canvas.Clear()
                if self.newCommand:
                    self.newCommand=False

            except:
                self.offscreen_canvas.Clear()
                self.newCommand=False
                continue

            
                
            offscreen_canvas=self.matrix.SwapOnVSync(self.offscreen_canvas)
    
    def bigClock(self):
        if datetime.now().weekday() == 0 or datetime.now().weekday() == 3 or datetime.now().weekday() == 6:
                graphics.DrawText(self.offscreen_canvas, font46, 21, 5, graphics.Color(128, 128,0), datetime.now().strftime('%A %b %d').upper())
        else:
                graphics.DrawText(self.offscreen_canvas, font46, 17, 5, graphics.Color(128, 128, 0 ), datetime.now().strftime('%A %b %d').upper())

        graphics.DrawText(self.offscreen_canvas, fontHR12, 24, 16, graphics.Color(128, 128, 128), datetime.now().strftime('%H:%M:%S'))

    def clockLineR(self):
        graphics.DrawText(self.offscreen_canvas, font46, 65, 5, graphics.Color(128, 128, 128), datetime.now().strftime('%H:%M:%S'))
    
    def countDownDisplay(self,font=font610,color=graphics.Color(255,255,0)):
        graphics.DrawText(self.offscreen_canvas, font610, 65, 15, color,(self.line3 or ""))

    def clockLine(self):
        """Simple clock mode: draw abbreviated weekday, date, and time."""
        graphics.DrawText(
            self.offscreen_canvas, font46, 6, 5, textColor,
            datetime.now().strftime('%a %m/%d   %H:%M:%S')
        )
    def staticLine1(self, font=font46, ypos=5, color=textColor, overScroll=30, delay=1, reset=False):
        """Draw the first line of text; scroll if it overflows.

        Parameters mirror those used for line2 below. `reset` will reposition
        scrolling to the start and pause briefly so a change is visible.
        """
        if overScroll and len(self.line1) > overScroll:
            self.line1pos, self.line1len
            self.line1len = len(self.line1)
            if reset:
                self.line1pos = 1
            if self.scrollCounter % delay == 0:
                self.line1pos -= 1
                if (self.line1pos + (self.line1len * 4) < 0):
                    self.line1pos = self.offscreen_canvas.width            
        else:
            self.line1pos = 0
        
        graphics.DrawText(self.offscreen_canvas, font, self.line1pos, ypos, color, (self.line1 or ""))
        if reset:
            time.sleep(2)

    def staticLine2(self, font=font46, ypos=14, color=textColor, overScroll=16, delay=1, reset=False):
        """Draw the second line of text; scrolling behaviour is similar to
        staticLine1 but uses a wider character spacing (6 pixels).
        """
        if overScroll and len(self.line2) > overScroll:
            self.line2pos, self.line2len
            # self.line2pos = 32
            self.line2len = len(self.line2)
            if reset:
                # self.line2pos = self.offscreen_canvas.width
                self.line2pos = 1
            if self.scrollCounter % delay == 0:
                self.line2pos -= 1
                if (self.line2pos + (self.line2len * 6) < 0):
                    # print("~~~~~~~~~~~~~repos")
                    self.line2pos = self.offscreen_canvas.width            
        else:
            self.line2pos = 0
        
        graphics.DrawText(self.offscreen_canvas, font, self.line2pos, ypos, color, (self.line2 or ""))
        if reset:
            time.sleep(2)

    def staticLine3(self):
        graphics.DrawText(self.offscreen_canvas, font46, 0, 15, textColor,(self.line3 or ""))

    def scrollLine1(self, delay=100, reset=False):
        """Scroll the first line continuously across the display.

        `delay` controls the scroll speed. When `reset` is True the text is
        repositioned to the right edge before scrolling begins.
        """
        self.line1pos, self.line1len
        if reset:
            self.line1pos = self.offscreen_canvas.width
        if self.scrollCounter % delay == 0:
            self.line1pos -= 1
            if (self.line1pos + self.line1len < 0):
                self.line1pos = self.offscreen_canvas.width

        self.line1len = graphics.DrawText(
            self.offscreen_canvas, font46, self.line1pos, 5, textColor, self.line1
        )

# Main function
if __name__ == "__main__":
    # configure MQTT callbacks and connect to broker
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(mqttBroker, 1883)
    mqttLoop = client.loop_start()  # start network loop in background

    # instantiate and run the LED sign application
    run_text = RunSign()
    if not run_text.process():
        run_text.print_help()
