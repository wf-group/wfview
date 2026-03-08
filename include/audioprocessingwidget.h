// Compatibility shim — class renamed to TxAudioProcessingWidget.
// This header is kept so any file that #includes "audioprocessingwidget.h"
// continues to compile unchanged.
#ifndef AUDIOPROCESSINGWIDGET_H
#define AUDIOPROCESSINGWIDGET_H
#include "txaudioprocessingwidget.h"
using AudioProcessingWidget = TxAudioProcessingWidget;
#endif // AUDIOPROCESSINGWIDGET_H
