#ifndef FUZZY_H
#define FUZZY_H

#include <QThread>
#include <vector>
#include "robot.h" //Robot class
#include "utils.h" //Utils library

struct Selector{
    Robot r1,r2;
};

class Fuzzy: public QThread
{
    Q_OBJECT
private:
    QMutex mutex;
    pVector enemy_pos_grid; //posicao em grid dos inimigos
    pVector team_pos_grid; //posicao em grid do time
    p2dVector enemy_pos; //posicao em cm dos inimigos
    Point  ball_pos_grid; //posicao em grid da bola
    Point2d ball_pos; //posicao em cm da bola
    Point2d centroid_atk; //posicao em cm do centro da area de atk
    Point2d centroid_def; //posicao em cm do centro da area de def

    dMatrix mi; //grau de pertinencia
    dMatrix limite;

    vector<double> pertinencia;
    vector<double> D;//Coeficiente de Disparo
    vector<double> y_output;
    vector<double> y_baixo;
    vector<double> y_medio;
    vector<double> y_alto;
    vector<double> d_universe;// Dominio dos termos primarios de entrada e saida
    vector<double> input; //vetor de entrada para calculo de decisao do fuzzy (FD,FC,FA)

    double output; //saida do fuzzy
    bool stop; //variavel de controle de thread

    Selector selec_robot; //estrutura de selecao dos robos que vao entrar no fuzzy



protected:
    void msleep(int ms);
    void run(); //rotina que a thread executa

public:
    Fuzzy();

    void init_duniverse();
    void init_funcao_pertinencia(int);
    void fuzzification();
    void calcula_input(Robot);
    double defuzzification();
    double min_function(double, double);
    double max_function(double, double);
    void decision();
    void Play();
    bool is_running();
    void Stop();
    bool isStopped() const;
    void set_to_select(Robot, Robot);
    void set_enemy_pos(p2dVector);
    void set_ball_pos(Point2d);
    void set_centroid_atk(Point2d);
    void set_centroid_def(Point2d);
    Point convert_C_to_G(Point2d);
    ~Fuzzy();
};


#endif // FUZZY_H
