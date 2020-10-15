#ifndef QLPCMDPARSER_H
#define QLPCMDPARSER_H

#include <QtCore>

class QlpCmdParser
{
public:
    QlpCmdParser(QString s);
    ~QlpCmdParser();

    bool getReload();
    int getQuality();
    double getFs();
    double getFa();
    bool getPageNext();
    bool getPageBack();
    int getPage();
    bool getFsUp();
    bool getFsDown();

private:
//    QHash<QString, QString> cmds;
    int quality = -1;
    bool reload = false;
    double fs = -1; // font scale
    double fa = -1; // font alpha from 0 to 1
    int page = -1;
    bool page_next = false;
    bool page_back = false;
    bool fs_up = false;
    bool fs_down = false;
};

#endif // QLPCMDPARSER_H
