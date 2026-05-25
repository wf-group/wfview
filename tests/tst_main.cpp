#include <QtTest>

int runControllerControllerTests(int argc, char **argv);
int runClusterTests(int argc, char **argv);
int runRigCtlCompatTests(int argc, char **argv);
int runRigProtocolTests(int argc, char **argv);

int main(int argc, char **argv)
{
    int status = 0;
    status |= runControllerControllerTests(argc, argv);
    status |= runClusterTests(argc, argv);
    status |= runRigCtlCompatTests(argc, argv);
    status |= runRigProtocolTests(argc, argv);
    return status;
}
