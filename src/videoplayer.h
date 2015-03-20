#ifndef VIDEOPLAYER_H
#define VIDEOPLAYER_H
#ifndef DISABLE_VIDEO

#include <QWidget>
#include <cstdio>


class VideoPlayer: public QWidget {
    Q_OBJECT

public:
    VideoPlayer( QWidget* parent = 0 );
    ~VideoPlayer();

    bool loadVideo(FILE* src, long len, bool loop);

public slots:
    void play();
    void stop();
    void setVolume(int vol);

signals:
    void videoFinished();
    void errorOccurred();

protected:
    void resizeEvent(QResizeEvent* e);

private:
    struct SDL_RWops* fRwops;
    long fDataLen;
    bool fLooping;
    friend class VideoPlayer_priv;
    class VideoPlayer_priv* d;
};


#endif
#endif
