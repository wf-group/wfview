#include <QtTest>
#include <QCoreApplication>

int runControllerControllerTests(int argc, char **argv);
int runAudioRoutingTests(int argc, char **argv);
int runClusterTests(int argc, char **argv);
int runIaxClientSessionTests(int argc, char **argv);
int runIcomScopeFrameAssemblerTests(int argc, char **argv);
int runModSourceTests(int argc, char **argv);
int runPeriodicModeTests(int argc, char **argv);
int runRadioTransportFrameTests(int argc, char **argv);
int runRigCtlCompatTests(int argc, char **argv);
int runRigCtlDTests(int argc, char **argv);
int runRigProtocolTests(int argc, char **argv);

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    int status = 0;
    status |= runAudioRoutingTests(argc, argv);
    status |= runControllerControllerTests(argc, argv);
    status |= runClusterTests(argc, argv);
    status |= runIaxClientSessionTests(argc, argv);
    status |= runIcomScopeFrameAssemblerTests(argc, argv);
    status |= runModSourceTests(argc, argv);
    status |= runPeriodicModeTests(argc, argv);
    status |= runRadioTransportFrameTests(argc, argv);
    status |= runRigCtlCompatTests(argc, argv);
    status |= runRigCtlDTests(argc, argv);
    status |= runRigProtocolTests(argc, argv);
    return status;
}
