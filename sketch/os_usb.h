/* команды компьютеру по проводу — работают в любой сборке */
void pcCmdUsb(const char *cmd) {
  size_t n = strlen(cmd);
  if (n > 32) n = 32;
  Serial.write('P');
  Serial.write((uint8_t)n);
  for (size_t i = 0; i < n; i++) Serial.write((uint8_t)cmd[i]);
}
