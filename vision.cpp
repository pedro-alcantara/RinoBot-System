#include <iostream>
#include <queue>
#include <vector>
#include <utility>
#include <cmath>
#include <ctime>
#include <cstdlib>
#include <QMessageBox>
#include "utils.h"
#include "vision.h"

using namespace std;

Point null_point = Point(-1, -1);

Vision::Vision(QObject *parent): QThread(parent)
{
    Point a, b;
    stop = true;
    showArea = sentPoints = teamsChanged = showErrors = showNames = ball_found = showCenters= false;
    mode = 0;
    cont = 0;
    tracker = Tracker::create("KCF");
    track_init.assign(7, false);
    objects_tracker.resize(7);
    robots.resize(6);
    robots[0].set_nick("Leona");
    robots[1].set_nick("Gandalf");
    robots[2].set_nick("Presto");
    robots[3].set_nick("T2");
    robots[4].set_nick("T2");
    robots[5].set_nick("T2");
    low.assign(3, 0);
    upper.assign(3, 255);
    ball_color.first.assign(3, 0);
    ball_color.second.assign(3, 255);
    info.enemy_robots.resize(3);
    info.team_robots.resize(3);
    info.ball_pos_cm = Point2d(0.0, 0.0);
    info.ball_pos = Point(0, 0);
    info.ball_last_pos = Point(0, 0);
    info.ball_found = false;

    if(!read_points("Config/map", map_points)){
        cerr << "The map could not be read from the file!" << endl;
    }
    if(!read_points("Config/attack_area", atk_points)){
        cerr << "The attack area could not be read from the file!" << endl;
    }
    if(!read_points("Config/defense_area", def_points)){
        cerr << "The defense area could not be read from the file!" << endl;
    }
    a = (map_points[4] + map_points[5])/2;
    b = (map_points[14] + map_points[13])/2;
    x_axis_slope = b - a;
    ball_pos = null_point;
    ball_last_pos = null_point;

    last_P = MatrixXd::Identity(3,3);
}

Mat Vision::detect_colors(Mat vision_frame, vector<int> low, vector<int> upper) //Detect colors in [low,upper] range
{
    Mat mask;

    //Generate mask with the points in the range
    inRange(vision_frame, Scalar(low[0],low[1],low[2]), Scalar(upper[0],upper[1],upper[2]), mask);

    //Attempt to remove noise (or small objects)
    morphologyEx(mask, mask, MORPH_OPEN, Mat(), Point(-1, -1), 2);
    morphologyEx(mask, mask, MORPH_CLOSE, Mat(), Point(-1, -1), 2);

    return mask;
}

bool invalid_contour(vector<Point> p){
    return p.size() == 0;
}

vector<Robot> Vision::fill_robots(vector<pMatrix> contours, vector<Robot> robots)
{
    size_t i, j, k, l;
    int csize, tsize, r_label = 0, min, t1size, tmin;
    double dista = 0.0, angle, last_angle;
    bool not_t1, error;
    Moments ball_moment, temp_moment;
    Point ball_cent = null_point, unk_robot, centroid, line_slope, last_cent;
    vector<bool> r_set;
    vector<vector<Moments> > r_m(3, vector<Moments>());
    vector<vector<Moments> > t_m(2, vector<Moments>());
    vector<pVector > r_col_cent(3, pVector());
    vector<pVector > tirj_cent(2, pVector());
    pair<Point, pair<int, int> > col_select;
    Vector3d pos_cam, last_pos;
    Vector2d v_w;
    pair<Matrix3d, Vector3d> kalman_res;

    //Get the ball moment from the contour
    if(contours[0].size() != 0){
        remove_if(contours[0].begin(), contours[0].end(), ball_area_limit);
        remove_if(contours[0].begin(), contours[0].end(), invalid_contour);
        sort(contours[0].begin(), contours[0].end(), sort_by_larger_area);
        ball_moment = moments(contours[0][contours[0].size()-1]);
        //Get ball centroid
        ball_cent = Point(ball_moment.m10/ball_moment.m00, ball_moment.m01/ball_moment.m00);
        ball_last_pos = ball_cent;
        ball_found = true;
    }else{
        ball_cent = ball_last_pos;
        ball_found = false;
    }

    ball_pos_cm.x = ball_pos.x * X_CONV_CONST;
    ball_pos_cm.y = ball_pos.y * Y_CONV_CONST;
    ball_pos_cm = ball_pos_cm;
    ball_pos = ball_cent;

    remove_if(contours[1].begin(), contours[1].end(), invalid_contour);
    sort(contours[1].begin(), contours[1].end(), sort_by_larger_area);
    remove_if(contours[1].begin(), contours[1].end(), area_limit);
    sort(contours[2].begin(), contours[2].end(), sort_by_larger_area);
    remove_if(contours[2].begin(), contours[2].end(), invalid_contour);
    remove_if(contours[2].begin(), contours[2].end(), area_limit);


    //Get the robots moments (their team color half)
    for(i = 0; i < 2; ++i){
        for(j = 0; j < contours[i+1].size(); ++j){
            temp_moment = moments(contours[i+1][j]);
            t_m[i].push_back(temp_moment);
            //Get centroid from robot team color half
            tirj_cent[i].push_back(Point(t_m[i][j].m10/t_m[i][j].m00, t_m[i][j].m01/t_m[i][j].m00));

        }
        if(contours[i+1].size() < 3){
            l = 3 - contours[i+1].size();
            for(j = 0; j < l; ++j){
                tirj_cent[i].push_back(null_point);
            }
        }
    }
    //cout << tirj_cent[1].size() << " robots found on team 2" << endl;
    //cout << tirj_cent[0].size() << " robots found on team 1" << endl;
    //Get the robots moments (their color half)
    for(i = 0; i < 3; ++i){
        remove_if(contours[i + 3].begin(), contours[i + 3].end(), invalid_contour);
        remove_if(contours[i + 3].begin(), contours[i + 3].end(), area_limit);
        csize = contours[i + 3].size();
        if(csize > 0){
            for(j = 0; j < csize; ++j){
                temp_moment = moments(contours[i + 3][j]);
                r_m[i].push_back(temp_moment);
            }
            //Get centroid from robot color half
            for(j = 0; j < csize; ++j){
                r_col_cent[i].push_back(Point(r_m[i][j].m10/r_m[i][j].m00, r_m[i][j].m01/r_m[i][j].m00));
            }

        }else{
            r_col_cent[i].push_back(null_point);
            //cout << robots[i].get_nick() << " not found!" << endl;
            robots[i].set_centroid(robots[i].get_from_pos_hist(0));
            robots[i].was_detected(false);
        }
    }

    r_set = vector<bool>(r_col_cent.size(), false);
    tsize = tirj_cent[0].size();
    t1size = (tirj_cent[1].size() < 3)?3+tirj_cent[1].size():6;

    //Define team 1 centroids and angles
    for(i = 0; i < tsize; ++i){
        unk_robot = tirj_cent[0][i];
        if(unk_robot == null_point) continue;
        not_t1 = false;
        col_select = make_pair(null_point, make_pair(-1, -1));

        for(j = 0, min = 20000; j < r_col_cent.size(); ++j){
            if(r_set[j]) continue;
            for(k = 0; k < r_col_cent[j].size(); ++k){
                if(r_col_cent[j][k] == null_point) continue;
                dista = euclidean_dist(unk_robot, r_col_cent[j][k]);
                if(dista < min && dista < 20){
                    min = dista;
                    tmin = min;
                    col_select = make_pair(r_col_cent[j][k], make_pair(j, k));
                }
            }
        }

        for(k = 3; k < t1size; ++k){    //Verify if the color assigned is not from the other team
            dista = euclidean_dist(tirj_cent[1][k-3], col_select.first);
            if(dista < tmin){
                not_t1 = true;
                break;
            }
        }

        r_label = col_select.second.first;
        if(r_label != -1){  //If the robot could be identified
            last_cent = robots[r_label].get_from_pos_hist(0);
            last_angle = robots[r_label].get_last_angle();
        }

        if(!not_t1 && r_label != -1){   //If the robot is from team 1 and he could be identified by the color half
            line_slope =  unk_robot - col_select.first;
            centroid = Point((unk_robot.x + col_select.first.x)/2, (unk_robot.y + col_select.first.y)/2);
            angle = fabs(angle_two_points(line_slope, x_axis_slope));
            angle = (col_select.first.y <= unk_robot.y)?angle:-angle;

            pos_cam << centroid.x * X_CONV_CONST / 100,
                       centroid.y * Y_CONV_CONST / 100,
                       angle;
            last_pos << last_cent.x * X_CONV_CONST / 100,
                        last_cent.y * Y_CONV_CONST / 100,
                        last_angle;
            v_w << 2,
                   2;

            kalman_res = kalman_filter(pos_cam, v_w, last_pos, 9, last_P);
            last_P = kalman_res.first;
            //centroid.x = kalman_res.second(0) * 100;
            //centroid.y = kalman_res.second(1) * 100;

            //angle = kalman_res.second(2);
            robots[r_label].set_team_cent(unk_robot);
            robots[r_label].set_color_cent(col_select.first);
            robots[r_label].set_line_slope(line_slope);
            robots[r_label].set_angle(angle);
            robots[r_label].set_centroid(centroid);
            robots[r_label].was_detected(true);
            r_set[r_label] = true;
       }else if(r_label != -1){
            robots[r_label].set_centroid(last_cent);
            robots[r_label].set_angle(last_angle);
            robots[r_label].was_detected(false);
            r_set[r_label] = true;
        }

        //cout << "Robo " << r_label << ", team cent = (" << unk_robot.x << "," <<unk_robot.y << "), "
        //    << "color cent= (" << col_select.first.x << "," << col_select.first.y << "), angle=" <<robots[i].get_angle() << endl;

    }

    error = false;
    for(i = 0; i < r_set.size(); ++i){
        if(!r_set[i]){
            error = true;
            if(showErrors) cerr << robots[i].get_nick() << " was not found!" << endl;
            robots[i].set_angle(robots[i].get_last_angle());
            robots[i].set_centroid(robots[i].get_from_pos_hist(0));
            robots[i].was_detected(false);
        }
    }

    //Define team 2 centroids and angles
    for(i = 3, j = 0, dista = INFINITY; i < 6; ++i, ++j){
        robots[i].set_team_cent(tirj_cent[1][i-3]);
        robots[i].set_centroid(robots[i].get_team_cent());
        robots[i].was_detected(true);
    }
    for(i = t1size; i < 6; ++i){
        robots[i].set_centroid(null_point);
        robots[i].was_detected(false);
    }


    if(error && showErrors) cerr << endl;

    cont++;
    return robots;
}

pair<vector<vector<Vec4i> >, vector<pMatrix> > Vision::detect_objects(Mat frame, vector<Robot> robots){
    int i, rsize = robots.size();
    Mat out_team1, out_team2, out_r[3], out_ball;
    vector<int> low, upper;
    vector<pMatrix> contours(6);
    vector<vector<Vec4i> > hierarchy(6);
    pair<vector<vector<Vec4i> >, vector<pMatrix> > ret;

    for(i = 0; i < rsize-3; ++i){
        low = robots[i].get_low_color();
        upper = robots[i].get_upper_color();
        out_r[i] = detect_colors(frame, low, upper);
    }

    low = robots[0].get_team_low_color();
    upper = robots[0].get_team_upper_color();

    out_team1 = detect_colors(frame, low, upper);

    low = robots[4].get_team_low_color();
    upper = robots[4].get_team_upper_color();

    out_team2 = detect_colors(frame, low, upper);

    low = ball_color.first;
    upper = ball_color.second;

    out_ball = detect_colors(frame, low, upper);

    findContours(out_ball, contours[0], hierarchy[0], RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));
    findContours(out_team1, contours[1], hierarchy[1], RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));
    findContours(out_team2, contours[2], hierarchy[2], RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));
    findContours(out_r[0], contours[3], hierarchy[3], RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));
    findContours(out_r[1], contours[4], hierarchy[4], RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));
    findContours(out_r[2], contours[5], hierarchy[5], RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0));

    ret.first = hierarchy;
    ret.second = contours;

    return ret;
}

Mat Vision::adjust_gamma(double gamma, Mat org)
{
    if(gamma == 1.0)
        return org;

    double inverse_gamma = 1.0 / gamma;

     Mat lut_matrix(1, 256, CV_8UC1);
     uchar * ptr = lut_matrix.ptr();
     for( int i = 0; i < 256; i++ )
       ptr[i] = (int)( pow( (double) i / 255.0, inverse_gamma ) * 255.0 );

     Mat result;
     LUT( org, lut_matrix, result );

    return result;
}

Mat Vision::CLAHE_algorithm(Mat org)    //Normalize frame histogram
{
    Mat lab_image, dst;
    vector<Mat> lab_planes(3);

    cvtColor(org, lab_image, CV_BGR2Lab);
    split(lab_image, lab_planes);

    //Apply CLAHE to the luminosity plane of the LAB frame
    Ptr<CLAHE> clahe = createCLAHE(2.0, Size(8,8));
    clahe->apply(lab_planes[0], dst);

    //Merge back the planes
    dst.copyTo(lab_planes[0]);
    merge(lab_planes, lab_image);

    cvtColor(lab_image, dst, CV_Lab2BGR);

    return dst;
}

Mat Vision::crop_image(Mat org){
    Mat cropped;
    Size size;
    Point2f pts[4], pts1[3], pts2[3];
    RotatedRect box;
    pVector roi(4), aux_y;

    pts2[0] = Point(0, 0);
    pts2[1] = Point(0, size.height-1);
    pts2[2] = Point(size.width-1, 0);

    aux_y = map_points;
    sort(map_points.begin(), map_points.end(), sort_by_smallest_x);
    sort(aux_y.begin(), aux_y.end(), sort_by_smallest_y);

    roi[0] = Point(map_points[0].x, aux_y[aux_y.size()-1].y);
    roi[3] = Point(map_points[map_points.size()-1].x, aux_y[0].y);
    roi[1] = Point(roi[0].x, roi[3].y);
    roi[2] = Point(roi[3].x, roi[0].y);

    box = minAreaRect(Mat(roi));
    box.points(pts);

    pts1[0] = pts[0];
    pts1[1] = pts[1];
    pts1[2] = pts[3];

    pts2[0] = Point(0, 0);
    pts2[1] = Point(box.boundingRect().width-1, 0);
    pts2[2] = Point(0, box.boundingRect().height-1);
    size = Size(box.boundingRect().width, box.boundingRect().height);

    if(!sentPoints){
        transf_matrix = getAffineTransform(pts1, pts2);
        transform(map_points, tmap_points, transf_matrix);
        transform(def_points, tdef_points, transf_matrix);
        transform(atk_points, tatk_points, transf_matrix);

        info.map_area = tmap_points;
        info.atk_area = tatk_points;
        info.def_area = tdef_points;

        sentPoints = true;
    }
    warpAffine(org, cropped, transf_matrix, size, INTER_LINEAR, BORDER_CONSTANT);

    return cropped;
}

Mat Vision::proccess_frame(Mat orig, Mat dest) //Apply enhancement algorithms
{
    dest = orig.clone();
    //Gamma correction
    dest = adjust_gamma(1.3 , dest);
    //Apply histogram normalization
    //dest = CLAHE_algorithm(dest);
    //Apply gaussian blur
     GaussianBlur(dest, dest, Size(5,5), 1.8);

     return dest;
}

Mat Vision::setting_mode(Mat raw_frame, Mat vision_frame, vector<int> low, vector<int> upper)   //Detect colors in [low,upper] range
{
    Mat mask, res;

    mask = detect_colors(vision_frame, low, upper);
    cvtColor(raw_frame, raw_frame, CV_BGR2RGB);
    raw_frame.copyTo(res, mask);

    return res;
}

Mat Vision::draw_robots(Mat frame, vector<Robot> robots)
{
    int i, size = robots.size();
    double angle;
    Point cent, team_cent, color_cent, inter;

    if(ball_pos != null_point)
        circle(frame, ball_pos, 10, Scalar(255, 0, 0));

    for(i = 0; i < size-3; ++i){
        cent = robots[i].get_centroid();
        team_cent = robots[i].get_team_cent();
        color_cent = robots[i].get_color_cent();
        angle = robots[i].get_angle();
        if(cent == null_point) continue;
        if(showCenters){
            circle(frame, team_cent, 5, Scalar(0, 255, 0), 1*(i+1));
            circle(frame, color_cent, 5, Scalar(0, 255, 0), 1*(i+1));
        }
        circle(frame, cent, 20, Scalar(0, 255, 0), 1.5);
        inter = Point(cent.x + 20 * cos(angle * PI / 180.0), cent.y - 20 * sin(angle * PI / 180.0));
        line(frame, cent, inter, Scalar(0, 255, 0), 1);

        if(showNames)
            putText(frame, robots[i].get_nick(), cent + Point(0, -2), FONT_HERSHEY_PLAIN, 1, Scalar(0, 255, 0), 2);
    }

    for(i = size-3; i < size; ++i){
        cent = robots[i].get_centroid();
        if(cent == null_point) continue;
        else cent += Point(2,2);
        circle(frame, cent , 20, Scalar(0, 0, 255), 1);
    }

    return frame;
}

void Vision::run()
{
    int delay = (1000/this->FPS);
    int i = 0, atk_size = atk_points.size(), map_size = map_points.size(), def_size = def_points.size();
    double elapsed_secs;
    clock_t begin, end;
    vector<pMatrix> obj_contours;
    Point def_cent, atk_cent;

    while(!stop){
        begin = clock();

        if(!cam.read(raw_frame)){
            stop = true;
            cerr << "A frame could not be read! (Vision)" << endl;
            return;
        }


        rows = raw_frame.rows;
        cols = raw_frame.cols;
        vision_frame = crop_image(raw_frame.clone());
        raw_frame = crop_image(raw_frame);
        vision_frame = proccess_frame(vision_frame, vision_frame);

        switch(mode){
            case 0: //Visualization mode
                obj_contours = detect_objects(vision_frame, robots).second;
                robots = fill_robots(obj_contours, robots);
                vision_frame = draw_robots(vision_frame, robots);

                cvtColor(vision_frame, vision_frame, CV_BGR2RGB);
                break;
            case 1: //Set color mode
                vision_frame = setting_mode(raw_frame, vision_frame, low, upper);

                break;
            default:
                break;
        }

        if(showArea && map_size > 0){
            //Draw map area points
            for(i = 0; i < map_size; ++i){
                circle(vision_frame, tmap_points[i], 1, Scalar(0,0,255), 2);
            }

            //Draw attack area points
            for(i = 0; i < atk_size; ++i){
                circle(vision_frame, tatk_points[i], 1, Scalar(255,0,0), 2);
            }

            atk_cent = (tatk_points[0]+tatk_points[7])/2;
            putText(vision_frame, "ATK Area", atk_cent, FONT_HERSHEY_PLAIN, 1, Scalar(255, 0, 0), 2);

            //Draw defense area points
            for(i = 0; i < def_size; ++i){
                circle(vision_frame, tdef_points[i], 1, Scalar(0,255,0), 2);
            }

            def_cent = (tdef_points[0]+tdef_points[7])/2;
            putText(vision_frame, "DEF Area", def_cent, FONT_HERSHEY_PLAIN, 1, Scalar(0, 255, 0), 2);
        }
        //cvtColor(raw_frame, raw_frame, CV_BGR2RGB);
        //img = QImage((uchar*)(raw_frame.data), raw_frame.cols, raw_frame.rows, raw_frame.step, QImage::Format_RGB888);
        img = QImage((uchar*)(vision_frame.data), vision_frame.cols, vision_frame.rows, vision_frame.step, QImage::Format_RGB888);
        end = clock();
        elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
        FPS = 1/elapsed_secs;

        info.enemy_robots[0] = robots[3];
        info.enemy_robots[1] = robots[4];
        info.enemy_robots[2] = robots[5];
        info.team_robots[0] = robots[0];
        info.team_robots[1] = robots[1];
        info.team_robots[2] = robots[2];
        info.ball_found = ball_found;
        if(ball_pos != null_point){
            info.ball_pos_cm = ball_pos_cm;
            info.ball_pos = ball_pos;
            info.ball_last_pos = ball_last_pos;
        }else{
            info.ball_pos_cm = Point2d(0.0, 0.0);
            info.ball_pos = Point(0, 0);
            info.ball_last_pos = Point(0, 0);
        }

        emit infoPercepted(info);
        emit processedImage(img);
        if(i%10 == 0){
            emit framesPerSecond(FPS);
            i = 0;
        }

        msleep(delay);
        i++;
    }
}

bool Vision::open_camera(int camid)
{
    if(!cam.isOpened()){
        cam.open(camid);
    }

    if(cam.isOpened()){
        FPS = 60;
        return true;
    }

    this->camid = camid;

    return false;
}

void Vision::Play()
{
    if(!isRunning()){
        if(isStopped())
            stop = false;
        start();
    }
}

void Vision::updateFuzzyRobots(rVector team_robots){
    robots[0].set_flag_fuzzy(team_robots[0].get_flag_fuzzy());
    robots[1].set_flag_fuzzy(team_robots[1].get_flag_fuzzy());
    robots[2].set_flag_fuzzy(team_robots[2].get_flag_fuzzy());
}

void Vision::updateMoverRobots(rVector team_robots){
    robots[0].set_lin_vel(make_pair(team_robots[0].get_l_vel(), team_robots[0].get_r_vel()));
    robots[1].set_lin_vel(make_pair(team_robots[1].get_l_vel(), team_robots[1].get_r_vel()));
    robots[2].set_lin_vel(make_pair(team_robots[2].get_l_vel(), team_robots[2].get_r_vel()));
}

void Vision::switch_teams_areas(){
    teamsChanged = (teamsChanged)?false:true;
}

vector<Robot> Vision::get_robots()
{
    return robots;
}

void Vision::set_robots(vector<Robot> robots)
{
    this->robots = robots;
}

void Vision::set_ball(pair<vector<int>, vector<int> > ball){
    this->ball_color = ball;
}

void Vision::set_low(vector<int> low)
{
    this->low = low;
}

void Vision::set_upper(vector<int> upper)
{
    this->upper = upper;
}

vector<int> Vision::get_low(){
    return low;
}

vector<int> Vision::get_upper(){
    return upper;
}

void Vision::set_mode(int m)
{
    mode = m;
}

void Vision::set_camid(int cam){
    this->camid = cam;
}

void Vision::Stop()
{
    stop = true;
}

bool Vision::isStopped() const
{
    return this->stop;
}

bool Vision::is_open()
{
    return cam.isOpened();
}

void Vision::msleep(int ms)
{
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000 * 1000};
    nanosleep(&ts, NULL);
}

void Vision::release_cam()
{
    cam.release();
}

int Vision::get_camID()
{
    return camid;
}

void Vision::show_area(bool show){
    showArea = show;
}

void Vision::show_names(bool show){
    showNames = show;
}

void Vision::show_centers(bool show){
    showCenters = show;
}

void Vision::show_errors(bool show){
    showErrors = show;
}

void Vision::save_image(){
    time_t t;
    string fname, path;

    srand((unsigned) time(&t));
    fname = to_string(rand() % 100000);
    fname = fname + "_img.jpg";
    path = "Img/" + fname;

    //cvtColor(raw_frame, to_save, CV_BGR2RGB);
    imwrite(path.c_str(), raw_frame);
}

void Vision::set_def_area(pVector def_points){
    this->tdef_points = def_points;
    //sentPoints = false;
}

void Vision::set_atk_area(pVector atk_points){
    this->tatk_points = atk_points;
    //sentPoints = false;
}

Vision::~Vision(){
    mutex.lock();
    stop = true;
    if(cam.isOpened())
        cam.release();
    condition.wakeOne();
    mutex.unlock();
    wait();
}
