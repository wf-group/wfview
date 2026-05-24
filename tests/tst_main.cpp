#include <QtTest>

int runControllerControllerTests(int argc, char **argv);
int runRigProtocolTests(int argc, char **argv);

int main(int argc, char **argv)
{
    int status = 0;
    status |= runControllerControllerTests(argc, argv);
    status |= runRigProtocolTests(argc, argv);
    return status;
}
