#include <QtTest>
#include <QCoreApplication>

int runControllerControllerTests(int argc, char **argv);
int runClusterTests(int argc, char **argv);
int runIaxClientSessionTests(int argc, char **argv);
int runRadioTransportFrameTests(int argc, char **argv);
int runRigCtlCompatTests(int argc, char **argv);
int runRigProtocolTests(int argc, char **argv);

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    int status = 0;
    status |= runControllerControllerTests(argc, argv);
    status |= runClusterTests(argc, argv);
    status |= runIaxClientSessionTests(argc, argv);
    status |= runRadioTransportFrameTests(argc, argv);
    status |= runRigCtlCompatTests(argc, argv);
    status |= runRigProtocolTests(argc, argv);
    return status;
}
