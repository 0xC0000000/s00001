#ifndef LED_MGR_H_
#define LED_MGR_H_

int InitLed();
void BlinkUploadStatusLed();
void TurnOffUploadStatusLed();
void TurnOnRfReaderErrLed();
void TurnOffRfReaderErrLed();
void ResetReader();

#endif /* LED_MGR_H_ */