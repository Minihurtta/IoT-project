/*
Built with:
Keil CLoud Studio
* Connecting with wired Ethernet to a 4G Mobile Router. IoT messaging
* with ARMmbed mbed-mqtt client.
* Hardware used
* ST NUCLEO-H743ZI2 or NXP FRDM K64F with wired 4G Mobile Router
* the USER button.
* A potentiometer as a sensor for testing, replaced by pressure sensor and a thermostat.
*
*/
#include "mbed.h"
// Ethernet
#include "EthernetInterface.h"
// Library to use https://github.com/ARMmbed/mbed-mqtt.
#include <MQTTClientMbedOs.h>
#include <string>

// Network interface
EthernetInterface net;
PwmOut led(D9);
AnalogIn temp(A0);
AnalogIn pot(A2);
float tempRead = 0; // Variables for different measurements
float PotRead = 0;
Thread thread1;
Thread thread2;

// AnalogIn pot(A3);
DigitalIn USERbtn(BUTTON1); //  BUTTON1 OR PC_13 in the NUCLEO-H743ZI2
// int potScaled = 0;
int btnState00 = false;
int btnState01 = false;
int sampleCount = 0;
time_t curTime_tt;
int disconnect = false;
char buffer[128];
float brightness = 0.0;
int warn = 0;
int oil_warning = 0;
int temp_warning = 0;
// Store device IP
SocketAddress deviceIP;
// Store broker IP
SocketAddress MQTTBroker;

TCPSocket socket;
MQTTClient client(&socket);

void led_blink()
{
    while (client.isConnected() == false)
    {
        brightness += 0.01;
        led = brightness;
        if (brightness >= 0.9f)
        {
            while (brightness >= 0.1f)
            {
                brightness -= 0.01;
                led = brightness;
                ThisThread::sleep_for(10ms);
            }
        }

        ThisThread::sleep_for(10ms);
    }
    led = 0;
}

void warning_blink()
{
    while (1)
    {
        brightness += 1;
        ThisThread::sleep_for(100ms); // Warning light if readings at alarming level
        brightness -= 1;
    }
}

void warn_scan()
{
    if (oil_warning == 1 || temp_warning == 1)
    {
        thread2.start(warning_blink);
    }
    else
    {
        thread2.terminate();
    }
}

int main()
{

    thread1.start(led_blink);
    // thread2.start(led_blink2);
    ThisThread::sleep_for(5s); // Waiting for the user to open serial terminal

    printf("\nConnecting network, getting IP with DHCP..\n");
    net.connect();

    // Show network address
    // SocketAddress netAddress;
    net.get_ip_address(&deviceIP);
    printf("\n\nThis is the board IP Address: %s\n", deviceIP.get_ip_address() ? deviceIP.get_ip_address() : "None");

    // Preparing the mqtt broker host name, broker port, mqtt message topic and mqtt message payload:
    net.gethostbyname("test.mosquitto.org", &MQTTBroker, NSAPI_IPv4, "net");

    MQTTBroker.set_port(1883);

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;

    // getting a new unique clientID if resetting or loading a new software version
    curTime_tt = time(NULL); // Get the current time on varaible of time type.
    printf("Time as seconds since powering the microcontroller = %d\n", curTime_tt);

    char IDstring[15]; // one byte for each character + 4 bytes for data type int
    sprintf(IDstring, "H743ZI2_8801_%d", curTime_tt);

    data.clientID.cstring = IDstring; // the unique clientID

    sprintf(buffer, "Hello from Mbed OS %d.%d", MBED_MAJOR_VERSION, MBED_MINOR_VERSION);

    MQTT::Message msg;
    msg.qos = MQTT::QOS0;
    msg.retained = false;
    msg.dup = false;
    msg.payload = (void *)buffer;
    msg.payloadlen = sizeof(buffer);

    ThisThread::sleep_for(5s);

    // Connecting mqtt broker
    printf("Connecting %s with clientID %s ...\n", "test.mosquitto.org", data.clientID.cstring);
    socket.open(&net);
    socket.connect(MQTTBroker);
    client.connect(data);

    // Publish
    printf("Publishing with payload length %d\n", sizeof(buffer));

    client.publish("iot/reede/hurme", msg); // MQTT topic
    if (client.isConnected())
    {
        thread1.terminate();
    }
    else
    {
        if (!thread1.Running)
        {
            thread1.start(led_blink);
        }
    }

    // while(1) {      // for ever....
    while (disconnect == false)
    { // A hundred publishing messages and disconnect
        // potScaled = pot.read_u16() >> 4;//MbedOS fills 16 bits starting on MSB
        // L432KC ADCs are 12bit.
        warn_scan();
        tempRead = temp.read() * 3.3;
        PotRead = pot.read() * 3.3;
        float volt_to_pressure = 10 / 4.5;
        float pressure = PotRead * volt_to_pressure;
        int mBar = pressure * 1000;
        int counter = 0;

        int tempDegree = (int)(100 * (tempRead / 0.01 - 0.5 / 0.01)); // change the value into celsius (For example: 2245 = 22,45 degrees celsius)

        btnState00 = USERbtn.read();
        sampleCount += 1;
        curTime_tt = time(NULL);

        if (mBar >= 5515)
        {
            oil_warning = 1;
            warn += 1;
        }
        else if (mBar <= 1378)
        {
            oil_warning = 1;
            warn = 1;
        }

        else
        {
            oil_warning = 0;
            warn = 0;
        }
        if (tempDegree >= 12000)
        {
            temp_warning = 1;
            warn += 1;
        }
        else
        {
            temp_warning = 0;
            warn = 0;
        }

        if (oil_warning || temp_warning > 0)
        {
            thread2.terminate();
            thread2.start(warning_blink);
        }
        else
        {
            thread2.terminate();
        }

        sprintf(buffer, "{\"d\":{\"Työkone numero\":01,\"SampleNr\":%d,\"UpTime\":%d,\"temp\":%d,\"Oil\":%d, \"Warn1\":\"%d\", \"Warn2\":\"%d\"}}", sampleCount, curTime_tt, tempDegree, mBar, temp_warning, oil_warning);

        msg.payload = (void *)buffer;
        msg.payloadlen = sizeof(buffer);
        // Publish
        printf("Publishing with payload length %d\n", sizeof(buffer));
        client.publish("Minfo/Konesilta/Vuokra1/Urak2/Status/All", msg);
        if (btnState00 == true && btnState01 == true)
        {                      // USER button pushed over two publishing intervals
            disconnect = true; // jumping out of the while loop and disconnecting
        }
        btnState01 = btnState00;

        while (counter == 0)
        {
            brightness += 0.01;
            led = brightness;
            if (brightness >= 1.0f)
            {
                while (brightness >= 0.0f)
                {
                    brightness -= 0.01;
                    led = brightness;
                    ThisThread::sleep_for(10ms);
                }
                counter = 1;
            }

            ThisThread::sleep_for(10ms);
        }
        ThisThread::sleep_for(10s); // Publishing interval 10 second
    }

    // client.yield(100);
    //  Disconnect this device client on the IBM Cloud Watson IoT
    printf("Disconnecting from MQTT broker");
    client.disconnect();
    ThisThread::sleep_for(2s);
    // Close the socket to free its memory and bring down the network interface
    socket.close();
    printf("Entering deepsleep (press RESET button to resume)\n");
    ThisThread::sleep_for(300s);
}