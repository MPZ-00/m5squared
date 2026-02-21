# fancy light

float t = millis() / 3000;
float sine = sin(t * PI) * 0.5 + 0.5;
int pwm = pow(sine, 2.2) * 255;
analogWrite(LED_PIN, pwm);