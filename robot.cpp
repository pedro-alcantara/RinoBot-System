#include <iostream>
#include <string>
#include <sstream>
#include "utils.h"
#include "robot.h"
#include "serial.h"

using namespace std;

Robot::Robot(){
    channel = -1;
    centroid = Point(-1, -1);
    centroid_cm = Point2d(0.0, 0.0);
    team_cent = Point(-1, -1);
    color_cent = Point(-1, -1);
    nick = "NULL";
    role = "NULL";
    ID = "NULL";
    low_color_team.assign(3, 0);
    upper_color_team.assign(3, 255);
    low_color.assign(3, 0);
    upper_color.assign(3, 255);
    pos_hist.push_back(Point(-1, -1));
    last_angle = loss_rate = 0.0;
    detected = false;
    flag_fuzzy = 0;
}

bool Robot::send_velocities(Serial *serial, pair<float, float> vels){
    float left_vel = vels.first, right_vel = vels.second;

    if(serial->is_open()){
        //Inicializamos o vetor de bytes a ser transferido
        QByteArray bytes(13, 0x0);
        bytes[0] = 18;
        //Setamos o byte 1 como o número do robô selecionado
        bytes[1] = (char)this->channel;
        bytes[12] = 19;

        //Criamos uma variável para converter a soma dos bytes de velocidade
        Short2Char cont;
        cont.Short = 0;

        //Criamos uma variável para converter as velocidades em bytes
        Float2Char valor;
        //Fazemos com que o valor float da nossa variável Union seja a velocidade informada
        valor.Float = left_vel;
        // Como o valor da variável Union ocupa a mesma posição de memória dos valores em byte dessa variável, setamos os bytes correspondentes da velocidade no vetor de saída como os bytes da variável Union
        bytes[2] = valor.Bytes[0];
        bytes[3] = valor.Bytes[1];
        bytes[4] = valor.Bytes[2];
        bytes[5] = valor.Bytes[3];
        // Adicionamos a soma dos bytes da velocidade a variável de contagem de bytes
        cont.Short += valor.Bytes[0] + valor.Bytes[1] + valor.Bytes[2] + valor.Bytes[3];

        valor.Float = right_vel;
        bytes[6] = valor.Bytes[0];
        bytes[7] = valor.Bytes[1];
        bytes[8] = valor.Bytes[2];
        bytes[9] = valor.Bytes[3];
        cont.Short += valor.Bytes[0] + valor.Bytes[1] + valor.Bytes[2] + valor.Bytes[3];

        //Setamos os bytes de contagemno vetor de saída como os bytes da variável de contagem
        bytes[10] = cont.Bytes[0];
        bytes[11] = cont.Bytes[1];

        //Escrevemos os bytes na porta serial
        serial->write_data(bytes);
        //Finalizamos a transferência dos dados a serial
        serial->flush();
    }else{
        cerr << nick << ": (Serial closed) Couldn't write wheels velocities at serial port." << endl;

        return false;
    }
    return true;
}

bool Robot::encoders_reading(Serial *serial, int &robot, pair<float, float> &vels, float &battery){
    //Array de bytes lidos da serial
    QByteArray *dados;
    //Armazena a quantidade de bytes lidos da serial
    unsigned char PosDados;
    char b;

    if(!serial || !serial->is_open()){
        cerr << "Couldn't read information from serial. (Serial closed)" << endl;
        return false;
    }

    // Executamos a leitura de bytes enquanto houver um byte disponível na serial
    while (serial->bytes_available() > 0){
        //Lemos um byte da serial para a variável b
        serial->read(&b, 1);
        //Adicionamos b ao array de entrada de bytes
        dados->data()[PosDados] = b;
        //Incrementamos a quantidade de bytes recebidos
        PosDados++;

        //Obtemos o número do robô
        int NumRobo = (int)dados->at(1);
        robot = NumRobo;

        if (PosDados == 9){
            //Se 9 bytes forem recebidos e o primeiro e o último byte são respectivamente 20 e 21. então esta é uma mensagem leitura da bateria
            if (dados->at(0) ==20 && dados->at(8)==21){
                unsigned short cont = 0;
                unsigned char i = 0;

                //Somamos os bytes do valor da bateria
                for (i=0x2; i <= 0x5; i+=0x1 ){
                    cont += (unsigned char)dados->at(i);
                }
                Short2Char conversor;
                //Obtemos a contagem de bytes enviada pelo robô
                conversor.Bytes[0] = dados->at(6);
                conversor.Bytes[1] = dados->at(7);
                //Se a contagem enviada pelo robô for igual a contagem feita acima, então os bytes chegaram corretamente
                if (cont == conversor.Short){
                    //Convertemos os bytes recebidos da bateria em um float
                    Float2Char bateria;
                    bateria.Bytes[0] = dados->at(2);
                    bateria.Bytes[1] = dados->at(3);
                    bateria.Bytes[2] = dados->at(4);
                    bateria.Bytes[3] = dados->at(5);
                    //Processamos o valor de bateria recebido
                    battery = bateria.Float;
                }
                //Informamos que a contagem atual de bytes recebidos é igual a zero
                PosDados = 0;
            }

        }else if (PosDados == 13){
            //Se 13 bytes forem recebidos e o primeiro e o último são respectivamente 18 e 19 então esta é uma mensagem de leitura das velocidades das rodas
             if (dados->at(0) ==18 && dados->at(12)==19){
                unsigned short cont =0;
                unsigned char i = 0;
                for (i=0x2; i <= 0x9; i+=0x1 )
                {
                  cont += (unsigned char)dados->at(i);
                }
                Short2Char conversor;
                conversor.Bytes[0] = dados->at(10);
                conversor.Bytes[1] = dados->at(11);
                if (cont == conversor.Short){
                    //Convertemos os bytes das velocidades em float
                    Float2Char velEsquerda, velDireita;
                    velEsquerda.Bytes[0] = dados->at(2);
                    velEsquerda.Bytes[1] = dados->at(3);
                    velEsquerda.Bytes[2] = dados->at(4);
                    velEsquerda.Bytes[3] = dados->at(5);
                    velDireita.Bytes[0] = dados->at(6);
                    velDireita.Bytes[1] = dados->at(7);
                    velDireita.Bytes[2] = dados->at(8);
                    velDireita.Bytes[3] = dados->at(9);

                    //Processamos os valores de velocidade recebidos
                    vels.first  = (double) velEsquerda.Float;
                    vels.second = (double) velDireita.Float;
                }
                PosDados = 0;
            }
        }
    }

    return true;
}

void Robot::set_angle(double angle)
{
    last_angle = this->angle;
    this->angle = angle;
}

double Robot::get_angle()
{
    return this->angle;
}

double Robot::get_last_angle()
{
    return this->last_angle;
}

void Robot::set_centroid(Point p)
{
    this->centroid = p;
    centroid_cm.x = centroid.x * X_CONV_CONST;
    centroid_cm.y = centroid.y * Y_CONV_CONST;

    add_pos_hist(p);
}

Point Robot::get_centroid()
{
    return this->centroid;
}


Point2d Robot::get_pos(){
    return centroid_cm;
}

void Robot::set_line_slope(Point p){
    this->line_slope = p;
}

Point Robot::get_line_slope(){
    return line_slope;
}

void Robot::add_pos_hist(Point p){
    if(pos_hist.size() == 5){
        pos_hist.pop_back();
    }
    pos_hist.push_back(p);
}

Point Robot::get_from_pos_hist(int rank){
    return pos_hist[pos_hist.size() - (rank + 1)];
}

void Robot::was_detected(bool detected){
    this->detected = detected;

    if((n_loss + n_detected)%500 == 0){
        loss_rate = n_loss / 500;
    }

    if(!detected){
        n_loss++;
    }else n_detected++;
}

bool Robot::is_detected(){
    return this->detected;
}

double Robot::get_loss_rate(){
    return loss_rate;
}

void Robot::set_color_cent(Point p)
{
    this->color_cent = p;
}

Point Robot::get_color_cent()
{
    return this->color_cent;
}

void Robot::set_team_cent(Point p)
{
    this->team_cent = p;
}

Point Robot::get_team_cent()
{
    return this->team_cent;
}

void Robot::set_channel(int channel)
{
    this->channel = channel;
}

void Robot::set_role(string role)
{
    this->role = role;
}

void Robot::set_nick(string nick)
{
    this->nick = nick;
}

void Robot::set_ID(string ID)
{
    this->ID = ID;
}

void Robot::set_team_low_color(vector<int> low_color){
    this->low_color_team = low_color;
}

void Robot::set_team_upper_color(vector<int> upper_color){
    this->upper_color_team = upper_color;
}

vector<int> Robot::get_team_low_color(){
    return this->low_color_team;
}

vector<int> Robot::get_team_upper_color(){
    return this->upper_color_team;
}

void Robot::set_low_color(vector<int> low_color)
{
    this->low_color = low_color;
}

void Robot::set_upper_color(vector<int> upper_color)
{
    this->upper_color = upper_color;
}

vector<int> Robot::get_low_color()
{
    return this->low_color;
}

vector<int> Robot::get_upper_color()
{
    return this->upper_color;
}

string Robot::get_nick()
{
    return this->nick;
}

string Robot::get_role()
{
    return this->role;
}

string Robot::get_ID()
{
    return this->ID;
}

int Robot::get_channel()
{
    return this->channel;
}

void Robot::set_flag_fuzzy(int output){

    if(output == 0)
    {
        flag_fuzzy = 0;
        cout << "Robo deve Defender Arduamente!"<< endl;
    }
    else if(output == 1)
    {
        flag_fuzzy = 1;
        cout << "Robo deve ser Um bom Meia!"<< endl;
    }
    else
    {
        flag_fuzzy = 2;
        cout << "Robo deve Atacar Ferozmente!"<< endl;
    }

}

int Robot::get_flag_fuzzy(){
    return  this->flag_fuzzy;

}

double Robot::min_function(double p, double q){
    if(p <= q)
    {
        return p;
    }
    else
        return q;
}

double Robot::max_function(double p, double q){
    if(p >= q)
    {
        return p;
    }
    else
        return q;
}

void Robot::set_lin_vel(pair<double, double> vels){
    this->vel = vels;
}

pair<double,double> Robot::get_lin_vel(){
    return this->vel;
}
