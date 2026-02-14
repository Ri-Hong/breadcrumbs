stateDiagram-v2
    [*] --> In_Pouch

    In_Pouch --> In_Pouch : Hall = 1
    In_Pouch --> Standby  : Hall = 0

    Standby --> Standby : Generic msg\nForward to next node

    Standby --> Waiting_For_Pickup : Pickup msg\nBlink LED + buzzer

    Waiting_For_Pickup --> In_Pouch : Hall = 1\nStop blinking\nSend pickup msg forward
