#include "resultswindow.h"

#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QKeyEvent>
#include <QApplication>
#include <QScreen>
#include <QPropertyAnimation>
#include <QTimer>
#include <QSettings>
#include <math.h>

#include "gradientcache.h"
#include "defines.h"
#include "contest.h"

namespace DJ
{
    namespace View
    {
        ResultsWindow::ResultsWindow(QWidget *parent) : QGraphicsView(parent)
        {
            setFrameShape(QFrame::NoFrame);

            this->started = false;
            this->offset = 0.0;
            this->canDoNextStep = true;
            this->resolvDone = false;
            this->lastResolvTeam = -1;
            this->currentResolvIndex = -1;

            this->scene = new QGraphicsScene(this);
            this->scene->setSceneRect(QApplication::primaryScreen()->geometry());
            this->scene->setBackgroundBrush(Qt::black);

            this->setScene(this->scene);
            this->setGeometry(QApplication::primaryScreen()->geometry());

            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            this->centerOn(0, 0);

            this->setViewportUpdateMode(FullViewportUpdate);
            this->setCacheMode(CacheBackground);
            this->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
            if (USE_OPENGL)
            {
                QSurfaceFormat format;
                format.setSamples(4); // Enable multisampling
                format.setDepthBufferSize(24);

                QOpenGLWidget *glWidget = new QOpenGLWidget;
                glWidget->setFormat(format);
                this->setViewport(glWidget);
            }
            else
            {
                this->setViewport(new QWidget);
            }

            this->headerItem = new HeaderGraphicsItem(QApplication::primaryScreen()->geometry().width());
            this->headerItem->setPos(0, 0);

            this->legendaItem = new LegendaGraphicsItem();
            QRectF legendaRect = this->legendaItem->boundingRect();
            this->legendaItem->setPos(QApplication::primaryScreen()->geometry().width() - legendaRect.width() - LEGENDA_RIGHT_OFFSET,
                                      QApplication::primaryScreen()->geometry().height() - legendaRect.height() - LEGENDA_BOTTOM_OFFSET);
            this->legendaItem->setZValue(1);

            this->winnerItem = new WinnerGraphicsItem;
            this->winnerItem->setPos(0, 0);
            this->winnerItem->setZValue(2);
            this->winnerItem->setOpacity(0);

            this->pixmap = new QGraphicsPixmapItem;
            this->pixmap->setZValue(1);

            this->scene->addItem(this->pixmap);
            this->scene->addItem(this->headerItem);
            this->scene->addItem(this->legendaItem);
            this->scene->addItem(this->winnerItem);
        }

        void ResultsWindow::setTeams(QList<ResultTeam> teams, bool animated, int lastResolvedTeam, int lastResolvedProblem, int currentTeam)
        {
            if (animated)
            {
                if (this->lastResolvTeam >= 0)
                {
                    this->teamItems.at(this->lastResolvTeam)->setHighlighted(false);
                }

                // Save all data
                this->teamsToSet = teams;
                this->lastResolvTeam = lastResolvedTeam;
                this->lastResolvProblem = lastResolvedProblem;
                this->currentResolvIndex = currentTeam;
                this->teamItems.at(this->lastResolvTeam)->setHighlighted(true);

                int screenHeight = QApplication::primaryScreen()->geometry().height();
                int itemToScrollHeight = HEADER_HEIGHT + (this->lastResolvTeam + 1) * TEAMITEM_HEIGHT + RESOLV_BELOW_OFFSET;
                int toScroll = qMax(0, itemToScrollHeight - screenHeight);
                QPointF toScrollPoint(0, toScroll);
                if (this->headerItem->pos().y() == -toScroll)
                {
                    // start a timer that waits a second
                    QTimer *timer = new QTimer;
                    timer->setSingleShot(true);
                    connect(timer, SIGNAL(timeout()), this, SLOT(timerDone()));
                    this->runningTimers.append(timer);
                    timer->start(TIME_TO_WAIT);
                }
                else
                {
                    // First, move to the the row to highlight
                    QParallelAnimationGroup *scrollToRowAnim = new QParallelAnimationGroup;
                    scrollToRowAnim->setProperty("DJ_animType", "toRow");
                    connect(scrollToRowAnim, SIGNAL(finished()), this, SLOT(animationDone()));

                    QPropertyAnimation *animHeader = new QPropertyAnimation(this->headerItem, "pos");
                    animHeader->setDuration(TIME_TO_SCROLL);
                    animHeader->setStartValue(this->headerItem->pos());
                    animHeader->setEndValue(QPointF(0, 0) - toScrollPoint);
                    scrollToRowAnim->addAnimation(animHeader);

                    for (int i = 0; i < this->teamItems.size(); i++)
                    {
                        QPropertyAnimation *animItem = new QPropertyAnimation(this->teamItems.at(i), "pos");
                        animItem->setDuration(TIME_TO_SCROLL);
                        QPointF startPoint;
                        startPoint.setX(0);
                        startPoint.setY(HEADER_HEIGHT + i * TEAMITEM_HEIGHT);
                        QPointF newPoint = startPoint;
                        newPoint -= toScrollPoint;
                        animItem->setStartValue(this->teamItems.at(i)->pos());
                        animItem->setEndValue(newPoint);
                        scrollToRowAnim->addAnimation(animItem);
                    }

                    this->runningAnimations.append(scrollToRowAnim);
                    scrollToRowAnim->start();
                }
            }
            else
            {
                for (int i = 0; i < this->teamItems.size(); i++)
                {
                    this->scene->removeItem(this->teamItems.at(i));
                    delete this->teamItems.at(i);
                }
                this->teamItems.clear();

                if (teams.size() == 0)
                {
                    return;
                }
                this->teams = teams;
                int numprobs = teams.at(0).problems.size();

                GradientCache::getInstance()->setNumProbs(numprobs);

                int probWidth = (NAME_WIDTH - (numprobs - 1) * PROB_MARGIN) / numprobs;

                for (int i = 0; i < teams.size(); i++)
                {
                    QList<ResultProblem> problems = teams.at(i).problems;
                    if (problems.size() == 0)
                    {
                        return;
                    }
                    QList<ProblemGraphicsItem *> problemItems;
                    for (int j = 0; j < numprobs; j++)
                    {
                        ProblemGraphicsItem *probItem = new ProblemGraphicsItem(0, probWidth);
                        probItem->setProblemId(problems.at(j).problemId);
                        probItem->setState(problems.at(j).state);
                        probItem->setNumTries(problems.at(j).numTries);
                        probItem->setTime(problems.at(j).time);
                        problemItems.append(probItem);
                    }

                    TeamGraphicsItem *teamItem = new TeamGraphicsItem(problemItems);
                    teamItem->setRank(teams.at(i).rank);
                    teamItem->setName(teams.at(i).name);
                    teamItem->setSolved(teams.at(i).solved);
                    teamItem->setTime(teams.at(i).time);
                    if (this->lastResolvTeam >= 0 && i >= this->lastResolvTeam && i < GOLD)
                    {
                        teamItem->setMedal(GOLD_MEDAL);
                    }
                    else if (this->lastResolvTeam >= 0 && i >= this->lastResolvTeam && i < SILVER)
                    {
                        teamItem->setMedal(SILVER_MEDAL);
                    }
                    else if (this->lastResolvTeam >= 0 && i >= this->lastResolvTeam && i < BRONZE)
                    {
                        teamItem->setMedal(BRONZE_MEDAL);
                    }
                    else
                    {
                        teamItem->setMedal(NO_MEDAL);
                    }
                    teamItem->setPos(0, HEADER_HEIGHT + i * TEAMITEM_HEIGHT + this->offset);
                    teamItem->setEven(i % 2 == 0);

                    this->teamItems.append(teamItem);
                    this->scene->addItem(teamItem);
                }
            }
        }

        void ResultsWindow::keyPressEvent(QKeyEvent *event)
        {
            switch (event->key())
            {
            case Qt::Key_Escape:
            case Qt::Key_Q:
            case Qt::Key_X:
                close();
                break;
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Space:
                if (this->canDoNextStep)
                {
                    doNextStep();
                }
            }
        }

        void ResultsWindow::mousePressEvent(QMouseEvent *event)
        {
            if (event->button() == Qt::LeftButton)
            {
                if (this->canDoNextStep)
                {
                    doNextStep();
                }
            }
        }

        void ResultsWindow::stopAnimations()
        {
            foreach (QAbstractAnimation *animation, this->runningAnimations)
            {
                animation->stop();
                delete animation;
            }
            this->runningAnimations.clear();
            foreach (QTimer *timer, this->runningTimers)
            {
                timer->stop();
                delete timer;
            }
            this->runningTimers.clear();
        }

        void ResultsWindow::reload()
        {
            if (USE_OPENGL)
            {
                QSurfaceFormat format;
                format.setSamples(4); // Enable multisampling
                format.setDepthBufferSize(24);

                QOpenGLWidget *glWidget = new QOpenGLWidget;
                glWidget->setFormat(format);
                this->setViewport(glWidget);
            }
            else
            {
                this->setViewport(new QWidget);
            }

            QString filename = BRANDING_IMAGE;
            // Update branding image
            if (!filename.isEmpty())
            {
                QPixmap pixmap(filename);
                if (!pixmap.isNull())
                {
                    this->pixmap->setPixmap(pixmap);
                }
                else
                {
                    this->pixmap->setPixmap(QPixmap());
                }
            }
            else
            {
                this->pixmap->setPixmap(QPixmap());
            }
            resizeImage();

            this->offset = 0.0;
            this->started = false;
            this->canDoNextStep = true;
            this->currentResolvIndex = -1;
            this->lastResolvTeam = -1;
            this->resolvDone = false;
            this->headerItem->setPos(0, 0);
            this->winnerItem->setOpacity(0);
            this->hideLegendAfterTimeout();
        }

        void ResultsWindow::setResolvDone()
        {
            this->resolvDone = true;
            this->canDoNextStep = true;
        }

        void ResultsWindow::hideLegendAfterTimeout()
        {
            this->legendaItem->setOpacity(1);
            QRectF legendaRect = this->legendaItem->boundingRect();
            this->legendaItem->setPos(QApplication::primaryScreen()->geometry().width() - legendaRect.width() - LEGENDA_RIGHT_OFFSET,
                                      QApplication::primaryScreen()->geometry().height() - legendaRect.height() - LEGENDA_BOTTOM_OFFSET);
            QTimer *legendaTimer = new QTimer(this);
            legendaTimer->setSingleShot(true);
            connect(legendaTimer, SIGNAL(timeout()), this, SLOT(hideLegenda()));
            this->runningTimers.append(legendaTimer);
            legendaTimer->start(LEGEND_WAIT_TIME);
        }

        void ResultsWindow::doNextStep()
        {
            if (!this->started)
            {
                this->canDoNextStep = false;
                QParallelAnimationGroup *scrollToBottomAnim = new QParallelAnimationGroup;
                connect(scrollToBottomAnim, SIGNAL(finished()), this, SLOT(animationDone()));

                int screenHeight = QApplication::primaryScreen()->geometry().height();
                int totalItemsHeight = HEADER_HEIGHT + this->teamItems.size() * TEAMITEM_HEIGHT + SCROLL_BELOW_OFFSET;
                int toScroll = qMax(0, totalItemsHeight - screenHeight);
                QPointF toScrollPoint(0, toScroll);

                if (toScroll == 0)
                {
                    delete scrollToBottomAnim;
                    this->started = true;
                    doNextStep();
                }
                else
                {
                    int timetoScroll = TIME_TO_WAIT + TIME_PER_ITEM * log(this->teamItems.size());

                    QPropertyAnimation *animHeader = new QPropertyAnimation(this->headerItem, "pos");
                    animHeader->setDuration(timetoScroll);
                    animHeader->setEasingCurve(QEasingCurve::OutBack);
                    animHeader->setStartValue(QPointF(0, 0));
                    animHeader->setEndValue(QPointF(0, 0) - toScrollPoint);
                    scrollToBottomAnim->addAnimation(animHeader);

                    for (int i = 0; i < this->teamItems.size(); i++)
                    {
                        QPropertyAnimation *animItem = new QPropertyAnimation(this->teamItems.at(i), "pos");
                        animItem->setDuration(timetoScroll);
                        animItem->setEasingCurve(QEasingCurve::OutBack);
                        QPointF startPoint;
                        startPoint.setX(0);
                        startPoint.setY(HEADER_HEIGHT + i * TEAMITEM_HEIGHT);
                        QPointF newPoint = startPoint;
                        newPoint -= toScrollPoint;
                        animItem->setStartValue(startPoint);
                        animItem->setEndValue(newPoint);
                        scrollToBottomAnim->addAnimation(animItem);
                    }

                    scrollToBottomAnim->setProperty("DJ_animType", "scrollToBottom");
                    this->runningAnimations.append(scrollToBottomAnim);
                    scrollToBottomAnim->start();
                }
            }
            else if (this->resolvDone)
            {
                this->canDoNextStep = false;
                if (this->teams.isEmpty())
                {
                    this->winnerItem->setWinner("No teams selected");
                }
                else
                {
                    ResultTeam winningTeam = this->teams.at(0);
                    this->winnerItem->setWinner(winningTeam.name);
                }
                QPropertyAnimation *winnerAnim = new QPropertyAnimation(this->winnerItem, "opacity");
                winnerAnim->setDuration(TIME_FOR_WINNER);
                winnerAnim->setStartValue(0);
                winnerAnim->setEndValue(1);
                winnerAnim->setProperty("DJ_animType", "winner");
                this->runningAnimations.append(winnerAnim);
                winnerAnim->start();
            }
            else
            {
                this->canDoNextStep = false;
                emit newStandingNeeded();
            }
        }

        void ResultsWindow::animationDone()
        {
            this->runningAnimations.removeAll((QAbstractAnimation *)this->sender());
            this->sender()->deleteLater();
            if (this->sender()->property("DJ_animType") == "scrollToBottom")
            {
                this->started = true;
                this->canDoNextStep = true;
            }
            else if (this->sender()->property("DJ_animType") == "toRow")
            {
                // start a timer that waits a second
                QTimer *timer = new QTimer;
                timer->setSingleShot(true);
                connect(timer, SIGNAL(timeout()), this, SLOT(timerDone()));
                this->runningTimers.append(timer);
                timer->start(TIME_TO_WAIT);
            }
            else if (this->sender()->property("DJ_animType") == "problemResolv")
            {
                TeamGraphicsItem *team = this->teamItems.at(this->lastResolvTeam);
                ProblemGraphicsItem *problem = team->getProblemGraphicsItem(this->lastResolvProblem);

                problem->setHighlighted(false);

                if (problem->isSolved())
                {
                    problem->setState(SOLVED);
                    // Determine where to move the row to
                    int moveTo = this->lastResolvTeam;
                    while (this->teamsToSet.at(moveTo).id != this->teams.at(this->lastResolvTeam).id)
                    {
                        moveTo--;
                    }
                    TeamGraphicsItem *teamThatMoves = this->teamItems.at(this->lastResolvTeam);
                    ResultTeam resultTeam = this->teamsToSet.at(moveTo);
                    teamThatMoves->setRank(resultTeam.rank);
                    teamThatMoves->setTime(resultTeam.time);
                    teamThatMoves->setSolved(resultTeam.solved);
                    int tme = TIME_TO_MOVE_INIT + TIME_TO_MOVE * (this->lastResolvTeam - moveTo);
                    if (tme == TIME_TO_MOVE_INIT)
                    {
                        QTimer *timer = new QTimer;
                        timer->setSingleShot(true);
                        connect(timer, SIGNAL(timeout()), this, SLOT(timerMoveUpDone()));
                        this->runningTimers.append(timer);
                        timer->start(TIME_TO_MOVE);
                    }
                    else
                    {
                        // Now move the current team to moveTo and move all teams from moveTo until the current team one down
                        QPointF moveToPoint = this->teamItems.at(moveTo)->pos();

                        QParallelAnimationGroup *moveAnim = new QParallelAnimationGroup;

                        QPropertyAnimation *moveUpAnim = new QPropertyAnimation(this->teamItems.at(this->lastResolvTeam), "pos");
                        connect(moveAnim, SIGNAL(finished()), this, SLOT(animationDone()));
                        moveUpAnim->setDuration(tme);
                        moveUpAnim->setStartValue(this->teamItems.at(this->lastResolvTeam)->pos());
                        moveUpAnim->setEndValue(moveToPoint);
                        moveAnim->addAnimation(moveUpAnim);

                        // Update standings
                        for (int i = moveTo; i < this->teamItems.size(); i++)
                        {
                            if (i != this->lastResolvTeam)
                            {
                                TeamGraphicsItem *team = this->teamItems.at(i);
                                ResultTeam resultTeamToSet;
                                if (i < this->lastResolvTeam)
                                {
                                    resultTeamToSet = this->teamsToSet.at(i + 1);
                                }
                                else
                                {
                                    resultTeamToSet = this->teamsToSet.at(i);
                                }
                                team->setRank(resultTeamToSet.rank);
                            }
                        }
                        for (int i = moveTo; i < this->lastResolvTeam; i++)
                        {
                            TeamGraphicsItem *team = this->teamItems.at(i);

                            QPropertyAnimation *moveDownAnim = new QPropertyAnimation(team, "pos");
                            moveDownAnim->setDuration(tme);
                            moveDownAnim->setStartValue(team->pos());
                            moveDownAnim->setEndValue(team->pos() + QPointF(0, TEAMITEM_HEIGHT));
                            moveAnim->addAnimation(moveDownAnim);
                        }

                        this->runningAnimations.append(moveAnim);
                        moveAnim->setProperty("DJ_animType", "moveTeam");
                        moveAnim->start();
                    }
                }
                else
                { // if the problem is not solved, just go to the next resolv
                    this->offset = this->headerItem->pos().y();
                    this->setTeams(this->teamsToSet);
                    doNextStep();
                }
            }
            else if (this->sender()->property("DJ_animType") == "moveTeam")
            {
                QTimer *timer = new QTimer;
                timer->setSingleShot(true);
                connect(timer, SIGNAL(timeout()), this, SLOT(timerMoveUpDone()));
                this->runningTimers.append(timer);
                timer->start(TIME_TO_MOVE_INIT);
            }
            else if (this->sender()->property("DJ_animType") == "winner")
            {
                // Do nothing...
            }
        }

        void ResultsWindow::timerDone()
        {
            this->runningTimers.removeAll((QTimer *)this->sender());
            this->sender()->deleteLater();
            if (this->lastResolvTeam == this->currentResolvIndex)
            {
                // Animate problem highlight
                TeamGraphicsItem *team = this->teamItems.at(this->lastResolvTeam);
                ProblemGraphicsItem *problem = team->getProblemGraphicsItem(this->lastResolvProblem);
                problem->setHighlighted(true);
                QPropertyAnimation *animHLC = new QPropertyAnimation(problem, "highlightColor");
                animHLC->setDuration(TIME_TO_BLINK);
                animHLC->setKeyValueAt(0, QColor(143, 124, 29));
                animHLC->setKeyValueAt(0.125, QColor(70, 62, 14));
                animHLC->setKeyValueAt(0.25, QColor(143, 124, 29));
                animHLC->setKeyValueAt(0.375, QColor(70, 62, 14));
                animHLC->setKeyValueAt(0.5, QColor(143, 124, 29));
                animHLC->setKeyValueAt(0.625, QColor(70, 62, 14));
                animHLC->setKeyValueAt(0.75, QColor(143, 124, 29));
                animHLC->setKeyValueAt(0.875, QColor(70, 62, 14));
                if (problem->isSolved())
                {
                    animHLC->setKeyValueAt(1, QColor(0, 128, 0));
                }
                else
                {
                    animHLC->setKeyValueAt(1, QColor(133, 0, 0));
                }

                QPropertyAnimation *animFC = new QPropertyAnimation(problem, "finalColor");
                animFC->setDuration(TIME_TO_BLINK);
                animFC->setKeyValueAt(0, QColor(255, 223, 54));
                animFC->setKeyValueAt(0.125, QColor(255, 223, 54));
                animFC->setKeyValueAt(0.25, QColor(255, 223, 54));
                animFC->setKeyValueAt(0.375, QColor(255, 223, 54));
                animFC->setKeyValueAt(0.5, QColor(255, 223, 54));
                animFC->setKeyValueAt(0.625, QColor(255, 223, 54));
                animFC->setKeyValueAt(0.75, QColor(255, 223, 54));
                animFC->setKeyValueAt(0.875, QColor(255, 223, 54));
                if (problem->isSolved())
                {
                    animFC->setKeyValueAt(1, QColor(0, 230, 0));
                }
                else
                {
                    animFC->setKeyValueAt(1, QColor(240, 0, 0));
                }
                QParallelAnimationGroup *parAnim = new QParallelAnimationGroup;
                parAnim->addAnimation(animHLC);
                parAnim->addAnimation(animFC);
                parAnim->setProperty("DJ_animType", "problemResolv");
                this->runningAnimations.append(parAnim);
                connect(parAnim, SIGNAL(finished()), this, SLOT(animationDone()));
                parAnim->start();
            }
            else
            {
                this->offset = this->headerItem->pos().y();
                this->setTeams(this->teamsToSet);
                this->teamItems.at(this->lastResolvTeam)->setHighlighted(true);
                if (this->lastResolvTeam >= NEED_TO_CLICK)
                {
                    doNextStep();
                }
                else
                {
                    this->canDoNextStep = true;
                }
            }
        }

        void ResultsWindow::timerMoveUpDone()
        {
            this->runningTimers.removeAll((QTimer *)this->sender());
            this->sender()->deleteLater();
            this->offset = this->headerItem->pos().y();
            this->setTeams(this->teamsToSet);
            doNextStep();
        }

        void ResultsWindow::resizeImage()
        {
            QSize size;
            if (!this->pixmap->pixmap().isNull())
            {
                size = this->pixmap->pixmap().size();
            }
            else
            {
                size = QSize(0, 0);
            }
            QRect screenSize = QApplication::primaryScreen()->geometry();
            QPointF labelPos;
            labelPos.setX(screenSize.width() - size.width() - BRANDING_IMAGE_OFFSET_X);
            labelPos.setY(screenSize.height() - size.height() - BRANDING_IMAGE_OFFSET_Y);
            this->pixmap->setPos(labelPos);
        }

        void ResultsWindow::hideLegenda()
        {
            this->runningTimers.removeAll((QTimer *)this->sender());
            this->sender()->deleteLater();
            QPropertyAnimation *legendaAnim = new QPropertyAnimation(this->legendaItem, "opacity");
            legendaAnim->setDuration(LEGEND_HIDE_TIME);
            legendaAnim->setEasingCurve(QEasingCurve::InOutExpo);
            legendaAnim->setStartValue(1);
            legendaAnim->setEndValue(0);

            this->runningAnimations.append(legendaAnim);
            legendaAnim->setProperty("DJ_animType", "legenda");
            legendaAnim->start();
        }

        int ResultsWindow::getCurrentResolvIndex()
        {
            return this->currentResolvIndex;
        }

        ResultTeam ResultsWindow::getResultTeam(int i)
        {
            return this->teams.at(i);
        }

        void ResultsWindow::setContest(Model::Contest *contest)
        {
            this->winnerItem->setContestName(contest->getName());
        }

    } // namespace View
} // namespace DJ
