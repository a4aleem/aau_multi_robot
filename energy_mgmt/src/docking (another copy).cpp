#include <docking.h>

using namespace std;

docking::docking()  // TODO(minor) create functions //TODO(minor) comments in .h
                    // file
{
    /* Load parameters */  // TODO(minor) checks if these params exist...
    ros::NodeHandle nh_tilde("~");
    nh_tilde.param("num_robots", num_robots, -1);
    nh_tilde.param("w1", w1, 0.25);
    nh_tilde.param("w2", w2, 0.25);
    nh_tilde.param("w3", w3, 0.25);
    nh_tilde.param("w4", w4, 0.25);  // TODO laod real values
    nh_tilde.getParam("x", origin_absolute_x);
    nh_tilde.getParam("y", origin_absolute_y);
    nh_tilde.param("distance_close", distance_close, 8.0);              // TODO add this parameter???
    nh_tilde.param<string>("move_base_frame", move_base_frame, "map");  // TODO remove this
    nh_tilde.param<string>("robot_prefix", robot_prefix, "");
    nh_tilde.param<std::string>("log_path", log_path, "");  // TODO add this parameter (to script)
    nh_tilde.param<int>("ds_selection_policy", ds_selection_policy, -1);
    nh_tilde.param<int>("auction_duration", auction_timeout, 5);
    nh_tilde.param<int>("reauctioning_timeout", reauctioning_timeout, 20);
    nh_tilde.param<float>("fiducial_signal_range", fiducial_signal_range, 10.0);  // TODO
    nh_tilde.param<bool>("fiducial_sensor_on", fiducial_sensor_on, true);         // not used at the moment...
    nh_tilde.param<float>("safety_coeff", safety_coeff, 0.8);                     // not used at the moment //TODO

    // TODO other checks
    if (num_robots < 1)
        ROS_FATAL("Invalid number of robots (%d)!", num_robots);
    if (ds_selection_policy < 0 || ds_selection_policy > 4)
        ROS_FATAL("Invalid docking station selection policy (%d)!", ds_selection_policy);

    /* Initialize robot name */
    if (robot_prefix.empty())
    {
        /* Empty prefix: we are on an hardware platform (i.e., real experiment) */
        ROS_INFO("Real robot");

        /* Set robot name and hostname */
        char hostname[1024];
        hostname[1023] = '\0';
        gethostname(hostname, 1023);
        robot_name = string(hostname);

        /* Set robot ID based on the robot name */
        std::string bob = "bob";
        std::string marley = "marley";
        std::string turtlebot = "turtlebot";
        std::string joy = "joy";
        std::string hans = "hans";
        if (robot_name.compare(turtlebot) == 0)
            robot_id = 0;
        if (robot_name.compare(joy) == 0)
            robot_id = 1;
        if (robot_name.compare(marley) == 0)
            robot_id = 2;
        if (robot_name.compare(bob) == 0)
            robot_id = 3;
        if (robot_name.compare(hans) == 0)
            robot_id = 4;

        my_prefix = "robot_" + SSTR(robot_id) + "/";  // TODO(minor)
    }
    else
    {
        /* Prefix is set: we are in a simulation */
        ROS_INFO("Simulation");

        robot_name = robot_prefix;  // TODO(minor) we need this? and are we sure that
                                    // it must be equal to robot_refix
                                    // (there is an unwanted '/' maybe...)

        /* Read robot ID number: to do this, it is required that the robot_prefix is
         * in the form "/robot_<id>", where
         * <id> is the ID of the robot */
        // TODO(minor) what if there are more than 10 robots and so we have
        // robot_10: this line of code will fail!!!
        robot_id = atoi(robot_prefix.substr(7, 1).c_str());

        /* Since we are in simulation and we use launch files with the group tag,
         * prefixes to topics are automatically
         * added: there is no need to manually specify robot_prefix in the topic
         * name */
        // my_prefix = "docking/"; //TODO(minor)
        my_prefix = "";
        // my_node = "energy_mgmt/"
        my_node = "";

        ROS_DEBUG("Robot prefix: %s; robot id: %d", robot_prefix.c_str(), robot_id);
    }

    /* Initialize robot struct */
    robot_t robot;
    robot.id = robot_id;
    robot.state = active;
    robots.push_back(robot);
    abs_to_rel(origin_absolute_x, origin_absolute_y, &robot.x, &robot.y);

    // TODO(minor) names (robot_0 end dockign)
    // TODO(minor) save names in variables

    /* SERVICE CLIENTS */
    /* Adhoc communication services */
    sc_send_auction =
        nh.serviceClient<adhoc_communication::SendEmAuction>(my_prefix + "adhoc_communication/send_em_auction");
    sc_send_docking_station = nh.serviceClient<adhoc_communication::SendEmDockingStation>(
        my_prefix + "adhoc_communication/send_em_docking_station");
    sc_send_robot = nh.serviceClient<adhoc_communication::SendEmRobot>(my_prefix + "adhoc_communication/send_em_robot");

    /* General services */
    sc_trasform = nh.serviceClient<map_merger::TransformPoint>("map_merger/transformPoint");  // TODO(minor)
    sc_robot_pose = nh.serviceClient<explorer::RobotPosition>(my_prefix + "explorer/robot_pose");
    sc_distance_from_robot = nh.serviceClient<explorer::DistanceFromRobot>(my_prefix + "explorer/distance_from_robot");
    sc_reachable_target = nh.serviceClient<explorer::DistanceFromRobot>(my_prefix + "explorer/reachable_target");
    sc_distance = nh.serviceClient<explorer::Distance>(my_prefix + "explorer/distance");

    /* Subscribers */
    sub_battery = nh.subscribe(my_prefix + "battery_state", 100, &docking::cb_battery, this);
    sub_robots = nh.subscribe(my_prefix + "robots", 100, &docking::cb_robots, this);
    sub_jobs = nh.subscribe(my_prefix + "frontiers", 10000, &docking::cb_jobs, this);
    sub_robot = nh.subscribe(my_prefix + "explorer/robot", 100, &docking::cb_robot, this);
    sub_docking_stations = nh.subscribe(my_prefix + "docking_stations", 100, &docking::cb_docking_stations, this);

    sub_auction_starting = nh.subscribe(my_prefix + "adhoc_communication/send_em_auction/new_auction", 100,
                                        &docking::cb_new_auction, this);
    sub_auction_reply =
        nh.subscribe(my_prefix + "adhoc_communication/send_em_auction/reply", 100, &docking::cb_auction_reply, this);
    sub_auction_winner_adhoc =
        nh.subscribe(my_prefix + "adhoc_communication/auction_winner", 100, &docking::cb_auction_result, this);

    sub_adhoc_new_best_ds = nh.subscribe("adhoc_new_best_docking_station_selected", 100, &docking::adhoc_ds, this);

    sub_charging_completed = nh.subscribe("charging_completed", 100, &docking::cb_charging_completed, this);

    sub_translate = nh.subscribe("translate", 100, &docking::cb_translate, this);
    sub_all_points = nh.subscribe("all_positions", 100, &docking::points, this);
    sub_check_vacancy = nh.subscribe("explorer/check_vacancy", 100, &docking::check_vacancy_callback, this);

    sub_check_vacancy = nh.subscribe("adhoc_communication/check_vacancy", 100, &docking::check_vacancy_callback, this);

    /* Publishers */
    pub_ds = nh.advertise<std_msgs::Empty>("docking_station_detected", 100);
    pub_new_target_ds =
        nh.advertise<geometry_msgs::PointStamped>("new_target_docking_station_selected", 100);  // to tell explorer...
    pub_adhoc_new_best_ds =
        nh.advertise<adhoc_communication::EmDockingStation>("adhoc_new_best_docking_station_selected", 100);
    pub_lost_own_auction = nh.advertise<std_msgs::Empty>("explorer/lost_own_auction", 100);
    pub_won_auction = nh.advertise<std_msgs::Empty>("explorer/won_auction", 100);
    pub_lost_other_robot_auction = nh.advertise<std_msgs::Empty>("explorer/lost_other_robot_auction", 100);

    pub_auction_result = nh.advertise<std_msgs::Empty>("explorer/auction_result", 100);

    pub_moving_along_path = nh.advertise<adhoc_communication::MmListOfPoints>("moving_along_path", 100);
    sub_next_ds = nh.subscribe("next_ds", 100, &docking::next_ds_callback, this);

    /* Timers */
    timer_finish_auction = nh.createTimer(ros::Duration(auction_timeout), &docking::timerCallback, this, true, false);
    timer_restart_auction =
        nh.createTimer(ros::Duration(10), &docking::timer_callback_schedure_auction_restarting, this, true, false);

    /* Variable initializations */
    robot_state = fully_charged;  // TODO(minor) param
    auction_winner = false;
    started_own_auction = false;
    update_state_required = false;
    moving_along_path = false;
    managing_auction = false;
    need_to_charge = false;  // TODO(minor) useless variable (if we use started_own_auction)
    recompute_graph = false;
    recompute_llh = false;
    llh = 0;
    local_auction_id = 0;
    participating_to_auction = 0;
    best_ds = NULL;
    target_ds = NULL;
    time_start = ros::Time::now();

    /* Function calls */
    preload_docking_stations();
    create_log_files();  // TODO(minor)

    // TODO(minor)
    // force sending a broadcast message
    /*
    adhoc_communication::SendEmRobot srv_msg;
    srv_msg.request.topic = "robots";  // TODO should this service be called also
    somewhere else?
    srv_msg.request.robot.id = robot_id;
    srv_msg.request.robot.state = fully_charged;
    sc_send_robot.call(srv_msg);
    */

    /*
    int arr[5][5] = {
        { 0, 1, 0, 6, 0 }, { 1, 0, 1, 8, 5 }, { 0, 1, 0, 1, 7 }, { 6, 8, 1, 0, 1
    }, { 0, 5, 7, 1, 0 },
    };
    std::vector < std::vector <int > > graph;
    for(int i=0; i < 5; i++) {
        std::vector<int> temp;
        for(int j=0; j < 5; j++)
            temp.push_back(arr[i][j]);
        graph.push_back(temp);
    }
    */

    // TODO(IMPORTANT) vary bad...
    /* Initialize docking station graph */
    for (int i = 0; i < undiscovered_ds.size(); i++)
    {
        std::vector<int> temp;
        for (int j = 0; j < undiscovered_ds.size(); j++)
            temp.push_back(-1);
        ds_mst.push_back(temp);
        ds_graph.push_back(temp);
    }

    if (DEBUG)
    {
        ros::Timer timer0 = nh.createTimer(ros::Duration(20), &docking::debug_timer_callback_0, this, false, true);
        debug_timers.push_back(timer0);
        ros::Timer timer1 = nh.createTimer(ros::Duration(20), &docking::debug_timer_callback_1, this, false, true);
        debug_timers.push_back(timer1);
        ros::Timer timer2 = nh.createTimer(ros::Duration(20), &docking::debug_timer_callback_2, this, false, true);
        debug_timers.push_back(timer2);

        debug_timers[robot_id].stop();
    }
}

void docking::preload_docking_stations()
{
    ROS_DEBUG("Preload DSs and mark them as undiscovered");

    int index = 0;  // index of the DS: used to loop above all the docking stations
                    // inserted in the file
    double x, y;    // DS coordinates

    /* If a x-coordinate of DS with index <index> is found, it means that that DS
     * is present in the file and must be
     * loaded */
    ros::NodeHandle nh_tilde("~");
    while (nh_tilde.hasParam("d" + SSTR(index) + "/x"))
    {
        /* Load coordinates of the new DS */
        nh_tilde.param("d" + SSTR(index) + "/x", x, 0.0);
        nh_tilde.param("d" + SSTR(index) + "/y", y, 0.0);

        /* Store new DS */
        ds_t new_ds;
        new_ds.id = index;
        abs_to_rel(x, y, &(new_ds.x), &(new_ds.y));
        new_ds.vacant = true;  // TODO(minor) param...
        undiscovered_ds.push_back(new_ds);

        /* Delete the loaded parameters (since they are not used anymore) */
        nh_tilde.deleteParam("d" + SSTR(index) + "/x");
        nh_tilde.deleteParam("d" + SSTR(index) + "/y");

        /* Prepare to search for next DS (if it exists) in the file */
        index++;
    }

    /* Print loaded DSs with their coordinates relative to the local reference system of the robot; only for debugging
     */  // TODO(minor) or better in global system?
    std::vector<ds_t>::iterator it;
    for (it = undiscovered_ds.begin(); it != undiscovered_ds.end(); it++)
        ROS_DEBUG("ds%d: (%f, %f)", (*it).id, (*it).x, (*it).y);
}

void docking::compute_optimal_ds()
{
    ROS_DEBUG("Compute currently optimal DS");

    /* Compute optimal DS only if at least one DS has been discovered (just for
     * efficiency and debugging) */
    if (ds.size() > 0)
    {
        /* Store currently optimal DS (for debugging ans safety checks)
         *
         * NB: even if the robot position does not change during the execution of
         *this function, the distance between the robot and a fixed point computed
         *using distance_from_robot() could change, because it calls a function in
         *explorer node that uses a costmap to compute the distance, and since the
         *costmap could be updated during the execution of compute_optimal_ds() (or,
         *more simply, the algorithm used to compute the distance is not 100%
         *precise), the distance could change: this means that we need to check at
         *the end of the computation of the new optimal DS if it is really a
         *different DS from the previous one; this can happens for instance because
         *the other robots are moving. Of course the differences between two
         *different calls of distance_from_robot with the same point as argument
         *will be very little, but they are enough to make us think that we have
         *found a DS that is better than the one already selected, even if the new
         *DS is exactly the old one. Another problems is that we could select a DS
         *that is not really the closest at the end of the execution of
         *compute_optimal_ds() because the distances could have changed, but we
         *accept this. */
        ds_t *old_optimal_ds = best_ds;

        // TODO(minor) functions
        /* "Closest" policy */
        if (ds_selection_policy == 0)  // TODO(minor) switch-case
            compute_closest_ds();

        /* "Vacant" policy */
        else if (ds_selection_policy == 1)
        {
            /* Check if there are vacant DSs. If there are, check also which one is the closest to the robot */
            bool found_vacant_ds = false;
            double min_dist = numeric_limits<int>::max();
            for (std::vector<ds_t>::iterator it = ds.begin(); it != ds.end(); it++)
            {
                if ((*it).vacant)
                {
                    /* We have just found a vacant DS (possibly the one already selected as optimal DS before calling
                     * this function).
                     *
                     * Notice that is is important to consider also the already selected optimal DS when we loop on all
                     *the DSs, since we have to update variable 'found_vacant_ds' to avoid falling back to "closest"
                     *policy, i.e., we cannot use a check like best_ds->id != (*it).id here */
                    found_vacant_ds = true;

                    /* Check if that DS is also closest than the currently optimal DS: if
                     * it is, we have a new optimal DS */
                    double dist = distance_from_robot((*it).x, (*it).y);
                    if (dist < 0)
                        continue;
                    if (dist < min_dist)
                    {
                        min_dist = dist;
                        best_ds = &(*it);
                    }
                }
            }

            /* If no DS is vacant at the moment, use "closest" policy to update the
             * optimal DS */
            if (!found_vacant_ds)
            {
                ROS_INFO("No vacant DS found: fall back to 'closest' policy");
                compute_closest_ds();
            }
        }

        /* "Opportune" policy */
        else if (ds_selection_policy == 2)
        {
            if (!moving_along_path)
            {
                /* Check if there are reachable DSs (i.e., DSs that the robot can reach
                 * with the remaining battery power
                 * without having to recharge first) with EOs */
                double min_dist = numeric_limits<int>::max();
                bool found_reachable_ds_with_eo = false, found_ds_with_eo = false;
                for (int i = 0; i < ds.size(); i++)
                    for (int j = 0; j < jobs.size(); j++)
                    {
                        double dist = distance(ds.at(i).x, ds.at(i).y, jobs.at(j).x, jobs.at(j).y);
                        if (dist < 0)
                            continue;
                        if (dist < battery.remaining_distance / 2)  // TODO ok?
                        {
                            /* We have found a DS with EOs */
                            found_ds_with_eo = true;

                            /* Check if that DS is also directly reachable (i.e., without
                             * recharging at intermediate DSs) */
                            double dist2 = distance_from_robot(ds.at(i).x, ds.at(i).y);
                            if (dist2 < 0)
                                continue;
                            if (dist2 < battery.remaining_distance)  // TODO coeff
                            {
                                /* We have found a DS that is directly reachable and with EOs */
                                found_reachable_ds_with_eo = true;

                                /* Check if it also the closest reachable DS with EOs */
                                if (dist2 < min_dist)  // TODO is what the paper ask?
                                {
                                    /* Update optimal DS */
                                    min_dist = dist2;
                                    best_ds = &ds.at(i);
                                }
                            }
                        }
                    }

                /* If there are no reachable DSs with EOs, check if there are DSs with
                 * EOs: if there are, compute a path on the graph of the DSs to reach
                 * one of these DSs, otherwise jsut use "closest" policy */
                if (!found_reachable_ds_with_eo)  // TODO check this part (in particular
                                                  // for the moving_along_path)
                {
                    if (found_ds_with_eo)
                    {
                        /* Compute a path formed by DSs that must be used for recharging, to
                         * reach a DS with EOs */
                        bool ds_found_with_mst = false;

                        if (!OPP_ONLY_TWO_DS)  // TODO
                        {
                            // compute closest DS with EOs // TODO are we sure that this is
                            // what the paper asks?
                            double min_dist = numeric_limits<int>::max();
                            ds_t *min_ds = NULL;
                            for (int i = 0; i < ds.size(); i++)
                            {
                                for (int j = 0; j < jobs.size(); j++)
                                {
                                    double dist = distance(ds.at(i).x, ds.at(i).y, jobs.at(j).x, jobs.at(j).y);
                                    if (dist < 0)
                                        continue;

                                    if (dist < battery.remaining_distance / 2)  // TODO ok?
                                    {
                                        double dist2 = distance_from_robot(ds.at(i).x, ds.at(i).y);
                                        if (dist2 < 0)
                                            continue;

                                        if (dist2 < min_dist)  // TODO is what the paper ask?
                                        {
                                            min_dist = dist2;
                                            min_ds = &ds.at(i);
                                        }
                                    }
                                }
                            }
                            if (min_ds == NULL)
                                return;  // this could happen if distance() always fails...

                            // compute closest DS
                            min_dist = numeric_limits<int>::max();
                            ds_t *closest_ds;
                            for (int i = 0; i < ds.size(); i++)
                            {
                                double dist = distance_from_robot(ds.at(i).x, ds.at(i).y);
                                if (dist < 0)
                                    continue;

                                if (dist < battery.remaining_distance)  // TODO(IMPORTANT) coeff
                                {
                                    min_dist = dist;
                                    closest_ds = &ds.at(i);
                                }
                            }

                            path.clear();
                            index_of_ds_in_path = 0;
                            ds_found_with_mst = find_path(ds_mst, closest_ds->id, min_ds->id, path);

                            if (ds_found_with_mst)
                            {
                                int closest_ds_id;

                                moving_along_path = true;

                                adhoc_communication::MmListOfPoints msg_path;  // TODO(minor)
                                                                               // maybe I can
                                                                               // pass directly
                                                                               // msg_path to
                                                                               // find_path...
                                for (int i = 0; i < path.size(); i++)
                                    for (int j = 0; j < ds.size(); j++)
                                        if (ds[j].id == path[i])
                                        {
                                            msg_path.positions[i].x = ds[j].x;
                                            msg_path.positions[i].y = ds[j].y;
                                        }

                                pub_moving_along_path.publish(msg_path);

                                // best_ds = min_ds;
                                for (int j = 0; j < ds.size(); j++)
                                    if (path[0] == ds[j].id)
                                    {
                                        best_ds = &ds[j];    // TODO VERY BAD!!!
                                        target_ds = &ds[j];  // TODO probably ok...;
                                    }
                            }
                            else
                                // closest policy //TODO hmm...
                                compute_closest_ds();
                        }

                        else  // only two ds... not really implemented...
                        {
                            if (!moving_along_path)
                            {
                                double first_step_x, first_step_y, second_step_x, second_step_y;
                                for (int i = 0; i < ds.size(); i++)
                                {
                                    bool existing_eo;
                                    for (int j = 0; j < jobs.size(); j++)
                                    {
                                        double dist = distance(ds.at(i).x, ds.at(i).y, jobs.at(j).x, jobs.at(j).y);
                                        if (dist < 0)
                                            continue;
                                        if (dist < battery.remaining_distance / 2)
                                        {
                                            existing_eo = true;
                                            for (int k = 0; k < ds.size(); k++)
                                            {
                                                dist = distance(ds.at(i).x, ds.at(i).y, ds.at(k).x, ds.at(k).y);
                                                if (dist < 0)
                                                    continue;
                                                if (k != i && dist < battery.remaining_distance / 2)
                                                {
                                                    double dist2 = distance_from_robot(ds.at(k).x, ds.at(k).y);
                                                    if (dist2 < 0)
                                                        continue;
                                                    if (dist2 < battery.remaining_distance / 2)
                                                    {
                                                        moving_along_path = true;
                                                        ds_found_with_mst = true;
                                                        first_step_x = ds.at(k).x;
                                                        first_step_y = ds.at(k).y;
                                                        second_step_x = ds.at(i).x;
                                                        second_step_y = ds.at(i).y;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }

                                if (ds_found_with_mst)
                                {
                                    moving_along_path = true;

                                    adhoc_communication::MmListOfPoints msg_path;
                                    msg_path.positions[0].x = first_step_x;
                                    msg_path.positions[0].y = first_step_y;
                                    msg_path.positions[1].x = second_step_x;
                                    msg_path.positions[1].y = second_step_y;

                                    pub_moving_along_path.publish(msg_path);
                                }
                            }
                        }
                    }
                    else
                        compute_closest_ds();  // TODO although probably the robot will think
                                               // that the exploration is over given how
                                               // explorer works for the moment...
                }
            }
            else
                ROS_ERROR("Already moving along path...");
        }

        /* "Current" policy */
        else if (ds_selection_policy == 3)
        {
            /* If no optimal DS has been selected yet, use "closest" policy, otherwise use "current" policy */  // TODO(minor)
                                                                                                                // assumption
            if (best_ds == NULL)
                compute_closest_ds();

            else
            {
                /* If the currently optimal DS has still EOs, keep using it, otherwise use
                 * "closest" policy */
                bool existing_eo = false;
                for (int i = 0; i < jobs.size(); i++)
                {
                    double dist = distance(best_ds->x, best_ds->y, jobs.at(i).x, jobs.at(i).y);
                    if (dist < 0)
                        continue;
                    if (dist < distance_close)  // TODO(IMPORTANT) ???
                    {
                        existing_eo = true;
                        break;
                    }
                }
                if (!existing_eo)
                    compute_closest_ds();
            }
        }

        /* "Flocking" policy */
        else if (ds_selection_policy == 4)
        {
            /* Get current robot position (once the service required to do that is ready)
             */  // TODO(minor) possibly
                                                                                           // reduntant...
            ros::service::waitForService("explorer/robot_pose");  // TODO(minor)
            explorer::RobotPosition srv_msg;
            if (sc_robot_pose.call(srv_msg))
            {
                robot.x = srv_msg.response.x;
                robot.y = srv_msg.response.y;
                ROS_DEBUG("Robot position: (%f, %f)", robot.x, robot.y);
            }
            else
            {
                ROS_ERROR("Call to service %s failed; not possible to compute optimal DS "
                          "for the moment",
                          sc_robot_pose.getService().c_str());
                return;
            }
            double min_cost = numeric_limits<int>::max();
            for (int d = 0; d < ds.size(); d++)
            {
                int count = 0;
                for (int i = 0; i < robots.size(); i++)
                    if (best_ds != NULL && robots.at(i).selected_ds == best_ds->id)  // TODO best_ds or target_ds??? best_ds != NULL?
                        count++;
                double n_r = (double)count / num_robots;

                int sum_x = 0, sum_y = 0;
                for (int i = 0; i < robots.size(); i++)
                {
                    sum_x += robots.at(i).x;
                    sum_y += robots.at(i).y;
                }
                double flock_x = sum_x / num_robots;
                double flock_y = sum_y / num_robots;
                double d_s = distance(ds.at(d).x, ds.at(d).y, flock_x, flock_y);  // TODO normalization

                if (d_s < 0)
                    continue;

                double swarm_direction_x = 0, swarm_direction_y = 0;
                for (int i = 0; i < robots.size(); i++)
                {
                    double robot_i_target_ds_x, robot_i_target_ds_y;

                    for (int k = 0; k < ds.size(); k++)
                        if (robots.at(i).selected_ds == ds.at(k).id)
                        {
                            robot_i_target_ds_x = ds.at(k).x;
                            robot_i_target_ds_y = ds.at(k).y;
                        }

                    swarm_direction_x += robot_i_target_ds_x - robots.at(i).x;
                    swarm_direction_y += robot_i_target_ds_y - robots.at(i).y;
                }
                // tan ( param * PI / 180.0 );
                double alpha, rho, theta;

                /*
                if(swarm_direction_x == 0)
                    if(swarm_direction_y == 0)
                        ROS_ERROR("Degenerate case");
                    else if(swarm_direction_y > 0)
                        rho = 90;
                    else
                        rho = 270;
                else {
                    double atan_val = atan( abs(swarm_direction_y) /
                abs(swarm_direction_x) * 180.0 / PI);
                    if(swarm_direction_y >= 0 && swarm_direction_x > 0)
                        rho = atan;
                    else if(swarm_direction_y >= 0 && swarm_direction_x < 0)
                        rho = 360 - atan;
                    else if(swarm_direction_y <= 0 && swarm_direction_x > 0)
                        rho = 360 - atan;
                    else if(swarm_direction_y <= 0 && swarm_direction_x < 0)
                        rho = atan;
                }
                alpha = atan( abs(ds.at(d).y - robot.y) / abs(ds.at(d).x - robot.x) );
                */

                rho = atan2(swarm_direction_y, swarm_direction_x) * 180 / PI;
                alpha = atan2((ds.at(d).y - robot.y), (ds.at(d).x - robot.x)) * 180 / PI;

                if (alpha > rho)
                    theta = alpha - rho;
                else
                    theta = rho - alpha;

                double theta_s = theta / 180;

                double d_f = numeric_limits<int>::max();
                for (int i = 0; i < jobs.size(); i++)
                {
                    double dist = distance(ds[d].x, ds[d].y, jobs[i].x, jobs[i].y);
                    if (dist < 0)
                        continue;
                    if (dist < d_f)
                        d_f = dist;
                }

                double cost = n_r + d_s + theta_s + d_f;
                if (cost < min_cost)
                {
                    min_cost = cost;
                    if (best_ds != NULL && ds[d].id != best_ds->id) //TODO best_ds != NULL
                        best_ds = &ds.at(d);
                }
            }
        }

        /* If a new optimal DS has been found, parameter l4 of the charging likelihood function must be updated, and the other robots must be informed */  // TODO and inform other robots
        if(best_ds == NULL) 
            ROS_DEBUG("No optimal DS has been selected yet");
        else if (old_optimal_ds == NULL || old_optimal_ds->id != best_ds->id)
        {
            if (old_optimal_ds != NULL)
                ROS_ERROR("Change optimal DS: ds%d -> ds%d", old_optimal_ds->id, best_ds->id);
            else
                ROS_ERROR("Select optimal DS: ds%d", best_ds->id);

            /* Keep track of the new optimal DS in log file */
            ros::Duration time = ros::Time::now() - time_start;
            fs_csv.open(csv_file.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
            fs_csv << time.toSec() << "," << best_ds->id << "," << num_robots << "," << ds_selection_policy << ","
                   << origin_absolute_x << "," << origin_absolute_y << std::endl;
            fs_csv.close();

            /* Update parameter l4 */
            update_l4();

            /* Inform other robots */  // TODO what if the robot signals a DS that is
                                       // unkown by one of the other robot
            // because it missed the message when the DS was discovered???
        }
        else
            ROS_DEBUG("Optimal DS unchanged");
    }
    else
        ROS_DEBUG("No DS has been discovered for the moment: optimal DS has not "
                  "been computed");
}

void docking::points(const adhoc_communication::MmListOfPoints::ConstPtr &msg)  // TODO(minor)
{
    ;
}

void docking::adhoc_ds(const adhoc_communication::EmDockingStation::ConstPtr &msg)  // TODO(minor)
{
    ;  // ROS_ERROR("adhoc_ds!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
}

double docking::get_llh()
{
    /* The likelihood can be updated only if the robot is not participating to an auction */  // TODO really
    // necessary???
    while (recompute_llh)
    {
        update_l1();
        update_l2();
        update_l3();
        update_l4();
    }

    if (participating_to_auction == 0)
        llh = w1 * l1 + w2 * l2 + w3 * l3 + w4 * l4;
    return llh;
}

void docking::update_l1()
{
    // count vacant docking stations
    int num_ds_vacant = 0;
    for (int i = 0; i < ds.size(); ++i)
    {
        if (ds[i].vacant == true)
            ++num_ds_vacant;
    }

    // count active robots
    int num_robots_active = 0;
    for (int i = 0; i < robots.size(); ++i)
    {
        if (robots[i].state == active)
            ++num_robots_active;
    }

    // sanity checks
    if (num_ds_vacant < 0)
    {
        ROS_ERROR("Invalid number of vacant docking stations: %d!", num_ds_vacant);
        l1 = 0;
        return;
    }
    if (num_robots_active < 0)
    {
        ROS_ERROR("Invalid number of active robots: %d!", num_robots_active);
        l1 = 1;
        return;
    }

    // check boundaries
    if (num_ds_vacant > num_robots_active)
    {
        l1 = 1;
    }
    else if (num_robots_active == 0)
    {
        l1 = 0;
    }

    // compute l1
    else
    {
        l1 = num_ds_vacant / num_robots_active;
    }
}

void docking::update_l2()
{
    double time_run = battery.remaining_time_run;
    double time_charge = battery.remaining_time_charge;  // TODO

    // sanity checks
    if (time_charge < 0)
    {
        ROS_ERROR("Invalid charging time: %.2f!", time_charge);
        l2 = 0;
        return;
    }
    if (time_run < 0)
    {
        ROS_ERROR("Invalid run time: %.2f!", time_run);
        l2 = 1;
        return;
    }
    if (time_run == 0 && time_charge == 0)
    {
        ROS_ERROR("Invalid run and charging times. Both are zero!");
        l2 = 1;
        return;
    }

    // compute l2
    l2 = time_charge / (time_charge + time_run);
}

void docking::update_l3()
{
    // count number of jobs: count frontiers and reachable frontiers
    int num_jobs = 0;
    int num_jobs_close = 0;
    for (int i = 0; i < jobs.size(); ++i)
    {
        ++num_jobs;
        double dist = distance_from_robot(jobs[i].x, jobs[i].y, true);
        if (dist < 0)
        {
            recompute_llh = true;
            return;
        }
        if (dist <= distance_close)  // use euclidean distance to make it faster //TODO
            ++num_jobs_close;
    }

    // sanity checks
    if (num_jobs < 0)
    {
        ROS_ERROR("Invalid number of jobs: %d", num_jobs);
        l3 = 1;
        return;
    }
    if (num_jobs_close < 0)
    {
        ROS_ERROR("Invalid number of jobs close by: %d", num_jobs_close);
        l3 = 1;
        return;
    }
    if (num_jobs_close > num_jobs)
    {
        ROS_ERROR("Number of jobs close by greater than total number of jobs: %d > %d", num_jobs_close, num_jobs);
        l3 = 0;
        return;
    }

    // check boundaries
    if (num_jobs == 0)
    {
        l3 = 1;
    }

    // compute l3
    else
    {
        l3 = (num_jobs - num_jobs_close) / num_jobs;
    }

    recompute_llh = false;
}

void docking::update_l4()
{
    // get distance to docking station
    double dist_ds = -1;
    for (int i = 0; i < ds.size(); ++i)
    {
        if (ds[i].id == best_ds->id)
        {
            dist_ds = distance_from_robot(ds[i].x, ds[i].y);
            if (dist_ds < 0)
            {
                recompute_llh = true;
                return;
            }
            break;
        }
    }

    // get distance to closest job
    int dist_job = numeric_limits<int>::max();
    for (int i = 0; i < jobs.size(); ++i)
    {
        int dist_job_temp = distance_from_robot(jobs[i].x, jobs[i].y,
                                                true);  // use euclidean distance to make it faster //TODO
        if (dist_job_temp < 0)
        {
            recompute_llh = true;
            return;
        }
        if (dist_job_temp < dist_job)
            dist_job = dist_job_temp;
    }

    if (ds.size() == 0 || jobs.size() == 0)
    {
        ROS_INFO("No DSs/jobs");
        l4 = 0;
        recompute_llh = false;
        return;
    }

    // sanity checks
    if (dist_job < 0 || dist_job >= numeric_limits<int>::max())
    {
        ROS_ERROR("Invalid distance to closest job: %d", dist_job);
        l4 = 0;
        return;
    }
    if (dist_job == 0 && dist_ds == 0)
    {
        ROS_ERROR("Invalid distances to closest job and docking station. Both are zero!");
        l4 = 0;
        return;
    }

    // compute l4
    l4 = dist_job / (dist_job + dist_ds);

    recompute_llh = false;
}

bool docking::auction_send_multicast(string multicast_group, adhoc_communication::EmAuction auction,
                                     string topic)  // TODO(minor) useless?
{
    adhoc_communication::SendEmAuction auction_service;

    string destination_name = multicast_group + robot_name;

    ROS_INFO("Sending auction to multicast group %s on topic %s", destination_name.c_str(), topic.c_str());
    ROS_ERROR("Sending auction to multicast group %s on topic %s", destination_name.c_str(), topic.c_str());
    auction_service.request.dst_robot = destination_name;
    auction_service.request.auction = auction;
    auction_service.request.topic = topic;

    if (sc_send_auction.call(auction_service))
    {
        if (auction_service.response.status)
        {
            ROS_INFO("Auction was transmitted successfully.");
            return true;
        }
        else
        {
            ROS_WARN("Failed to send auction to multicast group %s!", destination_name.c_str());
            return false;
        }
    }
    else
    {
        ROS_WARN("Failed to call service %s/adhoc_communication/send_auction [%s]", robot_name.c_str(),
                 sc_send_auction.getService().c_str());
        return false;
    }
}

double docking::distance_from_robot(double goal_x, double goal_y, bool euclidean)  // TODO
{
    if (euclidean)
        return distance(robot.x, robot.y, goal_x, goal_y, true);

    explorer::DistanceFromRobot srv_msg;
    srv_msg.request.x = goal_x;
    srv_msg.request.y = goal_y;

    ros::service::waitForService("explorer/distance_from_robot");
    for (int i = 0; i < 10; i++)
        if (sc_distance_from_robot.call(srv_msg) && srv_msg.response.distance >= 0)
            return srv_msg.response.distance;
        else
            ros::Duration(1).sleep();

    ROS_ERROR("Unable to compute distance at the moment...");
    return -1;

    /*
    tf::Stamped<tf::Pose> robotPose;
    if (!costmap->getRobotPose(robotPose))
    {
        ROS_ERROR("Failed to get RobotPose");
        return -1;
    }
    return distance_from_robot(robotPose.getOrigin().getX(),
    robotPose.getOrigin().getY(), goal_x, goal_y, euclidean);
    */
}

double docking::distance(double start_x, double start_y, double goal_x, double goal_y, bool euclidean)  // TODO
{
    // use euclidean distance
    if (euclidean)
    {
        double dx = goal_x - start_x;
        double dy = goal_y - start_y;
        return sqrt(dx * dx + dy * dy);
    }

    // calculate actual path length
    else
    {
        explorer::Distance srv_msg;
        srv_msg.request.x1 = start_x;
        srv_msg.request.y1 = start_y;
        srv_msg.request.x2 = goal_x;
        srv_msg.request.y2 = goal_y;

        ros::service::waitForService("explorer/distance");
        for (int i = 0; i < 10; i++)
            if (sc_distance.call(srv_msg) && srv_msg.response.distance >= 0)
                return srv_msg.response.distance;
            else
                ros::Duration(1).sleep();
    }

    ROS_ERROR("Unable to compute distance at the moment...");
    return -1;
}

void docking::cb_battery(const energy_mgmt::battery_state::ConstPtr &msg)
{
    ROS_DEBUG("Received battery state");

    /* Store new battery state */
    battery.charging = msg.get()->charging;
    battery.soc = msg.get()->soc;
    battery.remaining_time_charge = msg.get()->remaining_time_charge;
    battery.remaining_time_run = msg.get()->remaining_time_run;
    battery.remaining_distance = msg.get()->remaining_distance;

    /* Update parameter l2 of charging likelihood function */
    update_l2();
}

void docking::cb_robot(const adhoc_communication::EmRobot::ConstPtr &msg)  // TODO(minor) better name
{
    // TODO correctly update the state!!!!
    if (msg.get()->state == exploring || msg.get()->state == fully_charged || msg.get()->state == moving_to_frontier ||
        msg.get()->state == leaving_ds)
        robots[robot_id].state = active;
    else
        robots[robot_id].state = idle;

    if (msg.get()->state == in_queue)
    {
        ROS_ERROR("\n\t\e[1;34mRobot in queue!!!\e[0m");

        /* Schedule next auction (a robot goes in queue only if it has lost an
         * auction started by itself) NB: NOOOO!!! it could win is auction, then
         * immediately lose a following one that was sovrapposing with the one
         * started by it, so when both the auction are completed, the robot will
         * seem to be lost only an auction started by another robot!!!*/
        timer_restart_auction.setPeriod(ros::Duration(reauctioning_timeout), true);
        timer_restart_auction.start();
    }
    else if (msg.get()->state == going_charging)
    {
        ;  // ROS_ERROR("\n\t\e[1;34m Robo t going charging!!!\e[0m");
    }
    else if (msg.get()->state == charging)
    {
        ;                        // ROS_ERROR("\n\t\e[1;34mRechargin!!!\e[0m");
        need_to_charge = false;  // TODO ???

        set_target_ds_vacant(false);
    }
    else if (msg.get()->state == going_checking_vacancy)
    {
        ;  // ROS_ERROR("\n\t\e[1;34m going checking vacancy!!!\e[0m");
    }
    else if (msg.get()->state == checking_vacancy)
    {
        ;  // ROS_ERROR("\n\t\e[1;34m checking vacancy!!!\e[0m");
        adhoc_communication::SendEmDockingStation srv_msg;
        srv_msg.request.topic = "adhoc_communication/check_vacancy";
        srv_msg.request.docking_station.id = target_ds->id;  // target_ds, not best_ds!!!!!
        sc_send_docking_station.call(srv_msg);
    }
    else if (msg.get()->state == auctioning)
    {
        ROS_INFO("Robot need to recharge");
        need_to_charge = true;
        start_new_auction();  // TODO(IMPORTANT) only if the robot has not just won
                              // another auction
                              // TODO(improv) only if the robot is not already taking part to an
                              // auction...
    }
    else if (msg.get()->state == fully_charged || msg.get()->state == leaving_ds)
    {
        set_target_ds_vacant(true);
    }
    else if (msg.get()->state == moving_to_frontier || msg.get()->state == going_in_queue ||
             msg.get()->state == exploring)
    {
        ;  // ROS_ERROR("\n\t\e[1;34midle!!!\e[0m");
    }
    else if (msg.get()->state == finished)
    {
        finalize();
    }
    else
    {
        ROS_FATAL("\n\t\e[1;34m none of the above!!!\e[0m");
        return;
    }

    /*
    adhoc_communication::SendEmRobot srv_msg;
    srv_msg.request.topic = "robots";  // TODO should this service be called also
    somewhere else?
    srv_msg.request.robot.id = robot_id;
    srv_msg.request.robot.state = msg.get()->state;
    sc_send_robot.call(srv_msg);
    */

    robot_state = static_cast<state_t>(msg.get()->state);
}

void docking::cb_robots(const adhoc_communication::EmRobot::ConstPtr &msg)
{
    // ROS_ERROR("\e[1;34mReceived information from robot %d\e[0m",
    // msg.get()->id);
    if (DEBUG)
    {
        debug_timers[msg.get()->id].stop();
        debug_timers[msg.get()->id].setPeriod(ros::Duration(20), true);
        debug_timers[msg.get()->id].start();
        return;
    }

    ros::Duration time = ros::Time::now() - time_start;
    fs3_csv.open(csv_file_3.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    fs3_csv << time.toSec() << "," << msg.get()->id << "," << num_robots << "," << ds_selection_policy << ","
            << origin_absolute_x << "," << origin_absolute_y << std::endl;
    fs3_csv.close();

    // check if robot is in list already
    bool new_robot = true;
    for (int i = 0; i < robots.size(); ++i)
    {
        // robot is in list, update
        if (robots[i].id == msg.get()->id)
        {
            // update state
            if (msg.get()->state == exploring || msg.get()->state == fully_charged ||
                msg.get()->state == moving_to_frontier)
                robots[i].state = active;
            else
                robots[i].state = idle;

            robots[i].x = msg.get()->x;
            robots[i].y = msg.get()->y;
            robots[i].selected_ds = msg.get()->selected_ds;

            new_robot = false;
            break;
        }
    }

    // add new robot
    if (new_robot)
    {
        robot_t robot;
        robot.id = msg.get()->id;

        // update state
        if (msg.get()->state == exploring || msg.get()->state == fully_charged ||
            msg.get()->state == moving_to_frontier)
            robot.state = active;
        else
            robot.state = idle;

        robot.x = msg.get()->x;
        robot.y = msg.get()->y;
        robot.selected_ds = msg.get()->selected_ds;

        robots.push_back(robot);

        int count = 0;
        for (int i = 0; i < robots.size(); i++)
            count++;

        num_robots = count;  // TODO also works for simulation? isn't this already known?
    }

    // update charging likelihood
    update_l1();
}

void docking::cb_jobs(const adhoc_communication::ExpFrontier::ConstPtr &msg)
{
    adhoc_communication::ExpFrontierElement frontier_element;

    jobs.clear();

    // add new jobs
    for (int i = 0; i < msg.get()->frontier_element.size(); ++i)
    {
        frontier_element = msg.get()->frontier_element.at(i);

        // check if it is a new job
        bool new_job = true;
        /*
        for (int j = 0; j < jobs.size(); ++j)
        {
            if (frontier_element.id == jobs.at(j).id)
            {
                new_job = false;
                break;
            }
        }
        */

        // store job if it is new
        if (new_job == true)
        {
            job_t job;
            job.id = frontier_element.id;
            job.x = frontier_element.x_coordinate;
            job.y = frontier_element.y_coordinate;
            jobs.push_back(job);
        }
    }

    /*
        // remove completed jobs / frontiers
        for (int i = 0; i < jobs.size(); ++i)
        {
            // convert coordinates to cell indices
            unsigned int mx = 0;
            unsigned int my = 0;
            if (false)
            {
                // if(costmap->getCostmap()->worldToMap(jobs[i].x, jobs[i].y, mx,
       my) == false){
                ROS_ERROR("Could not convert job coordinates: %.2f, %.2f.",
       jobs[i].x, jobs[i].y);
                continue;
            }

            // get the 4-cell neighborhood with range 6
            vector<int> neighbors_x;
            vector<int> neighbors_y;
            for (int j = 0; j < 6; ++j)
            {
                neighbors_x.push_back(mx + i + 1);
                neighbors_y.push_back(my);
                neighbors_x.push_back(mx - i - 1);
                neighbors_y.push_back(my);
                neighbors_x.push_back(mx);
                neighbors_y.push_back(my + i + 1);
                neighbors_x.push_back(mx);
                neighbors_y.push_back(my - i - 1);
            }

            // check the cost of each neighbor
            bool unknown_found = false;
            bool obstacle_found = false;
            bool freespace_found = false;
            for (int j = 0; j < neighbors_x.size(); ++j)
            {
                int cost = 0;
                // unsigned char cost =
       costmap->getCostmap()->getCost(neighbors_x[j], neighbors_y[j]);
                if (cost == costmap_2d::NO_INFORMATION)
                {
                    unknown_found = true;
                }
                else if (cost == costmap_2d::FREE_SPACE)
                {
                    freespace_found = true;
                }
                else if (cost == costmap_2d::LETHAL_OBSTACLE)
                {
                    obstacle_found = true;
                }
            }

            // delete frontier if either condition holds
            //  * no neighbor is unknown
            //  * at least one neighbor is an obstacle
            //  * no neighbor is free space
            if (unknown_found == false || obstacle_found == true ||
       freespace_found == false)
            {
                jobs.erase(jobs.begin() + i);
                --i;
            }
        }

        // update charging likelihood
        update_l3();
        */
    update_l3();
}

void docking::cb_docking_stations(const adhoc_communication::EmDockingStation::ConstPtr &msg)
{
    // check if docking station is in list already
    bool new_ds = true;
    for (int i = 0; i < ds.size(); ++i)
    {
        // docking station is in list, update
        if (ds[i].id == msg.get()->id)
        {
            // coordinates don't match
            double x, y;
            abs_to_rel(msg.get()->x, msg.get()->y, &x, &y);
            if (ds[i].x != x || ds[i].y != y)
                ROS_ERROR("Coordinates of docking station %d do not match: (%.2f, "
                          "%.2f) != (%.2f, %.2f)",
                          ds[i].id, ds[i].x, ds[i].y, x, y);

            // update vacancy
            if (ds[i].vacant != msg.get()->vacant)
            {
                ds[i].vacant = msg.get()->vacant;
                ROS_ERROR("!!!!!!!!!!!New state for ds%d: %s", msg.get()->id,
                          (msg.get()->vacant ? "vacant" : "occupied"));
            }
            else
                ROS_ERROR("!!!!!!!!!!!State of ds%d unchanged (%s)", msg.get()->id,
                          (msg.get()->vacant ? "vacant" : "occupied"));

            new_ds = false;
            break;
        }
    }

    // add new docking station
    if (new_ds)
    {
        ds_t s;
        s.id = msg.get()->id;
        abs_to_rel(msg.get()->x, msg.get()->y, &s.x, &s.y);
        s.vacant = msg.get()->vacant;
        discovered_ds.push_back(s);
        ROS_ERROR("\e[1;34mNew docking station received: ds%d (%f, %f) \e[0m", s.id, s.x, s.y);

        /* Remove DS from the vector of undiscovered DSs */
        for (std::vector<ds_t>::iterator it = undiscovered_ds.begin(); it != undiscovered_ds.end(); it++)
            if ((*it).id == s.id)
            {
                undiscovered_ds.erase(it);
                break;
            }

        // map_merger::TransformPoint point;
        // point.request.point.src_robot = "robot_0";
        // point.request.point.x = s.x;
        // point.request.point.y = s.y;

        // ROS_ERROR("\e[1;34mCalling: %s\e[0m", sc_trasform.getService().c_str());
        // sc_trasform.call(point);
    }

    // update charging likelihood
    update_l1();
}

void docking::cb_new_auction(const adhoc_communication::EmAuction::ConstPtr &msg)  // TODO multihoop
{
    ROS_ERROR("Received bid for a new auction (%d)", msg.get()->auction);

    /*
    // set auction id
    if(id > auction_id) // it is a new action from another robot, respond
        auction_id = id;
    else if(id > 0){    // it is an old auction from another robot, ignore
        ROS_ERROR("Bad auction ID, it should be greater than %d!", auction_id);
        return false;
    }
    else                // it is a new auction by this robot
        ; //++auction_id; //TODO //F
    */

    /* Check if the robot has some interested in participating to this auction,
     * i.e., if the auctioned DS is the one
     * currently targetted by the robot */
    if (best_ds == NULL || msg.get()->docking_station != best_ds->id)
    {
        /* Robot received a bid of an auction whose auctioned docking station is not
         * the one the robot is interested in
         * at the moment, so it won't participate to the auction */
        ;  // ROS_INFO("Robot has no interested in participating to this auction");
    }
    else
    {
        if (get_llh() > msg.get()->bid)
        {
            /* The robot is interested in participating to the auction */
            participating_to_auction++;

            /* Start timer to force the robot to consider the auction concluded after
             * some time, even in case it does not
             * receive the result of the auction.
             * To keep the timer active, we must store it on the heap; the timer is
             * just pushed back in vector 'timers',
             * which is cleared when no auction is pending (and so no timer is
             * active). A better management should be
             * possible, for instance using insert(), at(), etc., but in previous
             * tries the node always crashed when using
             * these functions... */
            ros::Timer timer = nh.createTimer(ros::Duration(auction_timeout + 2),  // TODO do better...
                                              &docking::end_auction_participation_timer_callback, this, true, false);
            timer.start();
            timers.push_back(timer);

            ROS_INFO("The robot can bet an higher bid than the one received, so it "
                     "will participate to the auction");
            adhoc_communication::SendEmAuction srv;
            // srv.request.dst_robot = id; //TODO(minor) for multicasting...
            srv.request.topic = "adhoc_communication/send_em_auction/reply";
            srv.request.auction.auction = msg.get()->auction;
            srv.request.auction.robot = robot_id;
            srv.request.auction.docking_station = best_ds->id;
            srv.request.auction.bid = get_llh();
            ROS_DEBUG("Calling service: %s", sc_send_auction.getService().c_str());
            sc_send_auction.call(srv);
        }
        else
        {
            ROS_INFO("The robot has no chance to win, so it won't place a bid for "
                     "this auction");
        }
    }
}

void docking::cb_auction_reply(const adhoc_communication::EmAuction::ConstPtr &msg)  // TODO check
{
    ROS_ERROR("Received bid to auction %d", auction_id);

    if (auction_id != msg.get()->auction)
    {
        ROS_INFO("The received bid is not for the auction recently started by this "
                 "robot: ignore it");
        return;
    }

    if (!managing_auction)
    {
        ROS_INFO("The received bid has arrived too late, since the associated "
                 "auction has already finished: ignore it");
        return;
    }

    // probably this is unnecessary, but jsut to be safe...
    for (std::vector<auction_bid_t>::iterator it = auction_bids.begin(); it != auction_bids.end(); it++)
        if ((*it).robot_id == msg.get()->robot)
        {
            ROS_INFO("The robot has already received this bid: ignore it");
            return;
        }

    ROS_DEBUG("Store bid");
    auction_bid_t bid;
    bid.robot_id = msg.get()->robot;
    bid.bid = msg.get()->bid;
    auction_bids.push_back(bid);
}

void docking::end_auction_participation_timer_callback(const ros::TimerEvent &event)  // TODO what if instead the
                                                                                      // auction result are received
                                                                                      // after this timer? what if
                                                                                      // instead it is received a lot
                                                                                      // early?
{
    ROS_DEBUG("Force to consider auction concluded");
    participating_to_auction--;
}

void docking::timerCallback(const ros::TimerEvent &event)  // TODO check
{
    /* The auction is concluded; the robot that started it has to compute who is
     * the winner and must inform all the
     * other robots */
    ROS_INFO("Auction timeout: compute auction winner...");

    // ??? //TODO
    managing_auction = false;

    /* Compute auction winner: loop through all the received bids and find the
     * robot that sent the highest one */
    int winner;
    float winner_bid = numeric_limits<int>::min();
    std::vector<auction_bid_t>::iterator it = auction_bids.begin();
    for (; it != auction_bids.end(); it++)
    {
        ROS_DEBUG("Robot %d placed %f", (*it).robot_id, (*it).bid);
        if ((*it).bid > winner_bid)
        {
            winner = (*it).robot_id;
            winner_bid = (*it).bid;
        }
    }

    // TODO

    if (auction_bids.size() < 3 && ds.size() == 1)
        ;  // ROS_ERROR("\n\t\e[1;34m OH NO!!!!!!!!!!!!!!!!!!!!!!!!!!!!! \e[0m");

    ROS_DEBUG("The winner is robot %d", winner);

    /* Delete stored bids to be able to start another auction in the future */
    auction_bids.clear();  // TODO inefficient!!!!

    /* Check if the robot that started the auction is the winner */
    if (winner == robot_id)
    {
        /* The robot won its own auction */
        ROS_INFO("Winner of the auction");  // TODO specify which auction

        auction_winner = true;
        timer_restart_auction.stop();  // TODO i'm not sure that this follows the
                                       // idea in the paper... //TODO no! jsut put a
                                       // check in the timer callback...
    }
    else
    {
        /* The robot lost its own auction */
        ROS_ERROR("Robot lost its own auction");
        auction_winner = false;
    }

    adhoc_communication::SendEmAuction srv_mgs;
    srv_mgs.request.topic = "adhoc_communication/auction_winner";
    srv_mgs.request.auction.auction = auction_id;
    srv_mgs.request.auction.robot = winner;

    srv_mgs.request.auction.docking_station = best_ds->id;
    srv_mgs.request.auction.bid = get_llh();

    // ROS_ERROR("\n\t\e[1;34m%s\e[0m", sc_send_auction.getService().c_str());
    sc_send_auction.call(srv_mgs);

    /* Computation completed */
    participating_to_auction--;
    update_state_required = true;
}

void docking::cb_charging_completed(const std_msgs::Empty &msg)  // TODO(minor)
{
    ;
}

void docking::timer_callback_schedure_auction_restarting(const ros::TimerEvent &event)
{
    started_own_auction = true;  // otherwise a robot could not start the auction
                                 // because the following if is true, then win
                                 // another robot auction and stop the time, then
                                 // lost another and not be reset in queue...
    ROS_ERROR("Periodic re-auctioning");
    if (participating_to_auction == 0)  // Notice that it is still possible that
                                        // two robots start an auction at the same
                                        // time...
        start_new_auction();
    else
    {
        ROS_ERROR("Robot is already participating to an auction: let's wait "
                  "instead of starting another one...");
        timer_restart_auction.setPeriod(ros::Duration(reauctioning_timeout), true);
        timer_restart_auction.start();
    }
}

void docking::start_new_auction()
{
    if (best_ds == NULL)
    {
        ROS_FATAL("The robot needs to recharge, but it doesn't know about any "
                  "existing DS!");  // TODO improve...
        return;
    }

    ROS_INFO("Starting new auction");
    started_own_auction = true;

    /* The robot is starting an auction */
    managing_auction = true;  // TODO(minor)
    participating_to_auction++;

    /* Start auction timer to be notified of auction conclusion */
    timer_finish_auction.setPeriod(ros::Duration(auction_timeout), true);
    timer_finish_auction.start();

    /* Send broadcast message to inform all robots of the new auction */
    adhoc_communication::SendEmAuction srv;
    srv.request.topic = "adhoc_communication/send_em_auction/new_auction";
    srv.request.auction.auction = next_auction_id();
    srv.request.auction.robot = robot_id;
    srv.request.auction.docking_station = best_ds->id;
    srv.request.auction.bid = get_llh();
    ROS_DEBUG("Calling service: %s", sc_send_auction.getService().c_str());
    sc_send_auction.call(srv);

    /* Keep track of robot bid */
    auction_bid_t bid;
    bid.robot_id = robot_id;
    bid.bid = get_llh();
    auction_bids.push_back(bid);
}

void docking::cb_translate(const adhoc_communication::EmDockingStation::ConstPtr &msg)  // TODO(minor)
{
    if (robot_prefix == "/robot_1")  // TODO ...
    {
        // ROS_ERROR("\n\t\e[1;34mReply to translation\e[0m");
        map_merger::TransformPoint point;

        point.request.point.src_robot = "robot_0";
        point.request.point.x = msg.get()->x;
        point.request.point.y = msg.get()->y;
        // if(sc_trasform.call(point)) ROS_ERROR("\e[1;34mTransformation
        // succeded:\n\t\tOriginal point: (%f,
        // %f)\n\t\tObtained point: (%f, %f)\e[0m",point.request.point.x,
        // point.request.point.y, point.response.point.x,
        // point.response.point.y);
    }
}

void docking::cb_auction_result(const adhoc_communication::EmAuction::ConstPtr &msg)
{
    if (best_ds == NULL)
    {
        ROS_ERROR("The robot does not know about any existing DS!");  // TODO it
                                                                      // means that
                                                                      // it missed
                                                                      // some
                                                                      // messages!!
        return;
    }

    // TODO the robot must check for its participation to the auction!!!

    // ROS_INFO("Received result of auction ... //TODO(minor) complete!!

    /* Check if the robot is interested in the docking station that was object of
     * the auction whose result has been just
     * received */
    // TODO(minor) use multicast instead!!!
    if (msg.get()->docking_station == best_ds->id)  // TODO check if the robot already knows this DS!
    {
        ROS_INFO("Received result of an auction to which the robot participated");  // TODO(minor)
                                                                                    // acutally
                                                                                    // maybe
                                                                                    // it
                                                                                    // didn't participate because its
                                                                                    // bid was lost...

        /* Since the robot received the result of an auction to which it took part,
         * explorer node must be informed of a
         * possible change in the robot state */
        update_state_required = true;

        /* Check if the robot is the winner of the auction */
        if (robot_id == msg.get()->robot)
        {
            /* The robot won the auction */
            ROS_INFO("Winner of the auction started by another robot");
            auction_winner = true;
            lost_other_robot_auction = false;  // TODO(minor) redundant?

            // TODO and what if best_ds is updated just a moment before starting the
            // auction??? it shoul be ok because
            // the if above would be false
            timer_restart_auction.stop();  // TODO  i'm not sure that this follows the
                                           // idea in the paper... but probably
                                           // this is needed otherwise when i have many pendning auction and with a
                                           // timeout enough high, i could have an inifite loop of restarting
                                           // auctions... or I could but a control in the timer_callback!!!
        }
        else
        {
            /* The robot has lost an auction started by another robot (because the
             * robot that starts an auction does not
             * receive the result of that auction with this callback */
            // TODO should check if the robto took part to the auction
            ROS_INFO("Robot didn't win this auction started by another robot");
            auction_winner = false;
            lost_other_robot_auction = true;
        }
    }
    else
        ROS_DEBUG("Received result of an auction the robot was not interested in: "
                  "ignoring");
}

void docking::abs_to_rel(double absolute_x, double absolute_y, double *relative_x, double *relative_y)
{
    *relative_x = absolute_x - origin_absolute_x;
    *relative_y = absolute_y - origin_absolute_y;
}

void docking::rel_to_abs(double relative_x, double relative_y, double *absolute_x, double *absolute_y)
{
    *absolute_x = relative_x + origin_absolute_x;
    *absolute_y = relative_y + origin_absolute_y;
}

void docking::check_vacancy_callback(const adhoc_communication::EmDockingStation::ConstPtr &msg)  // TODO(minor) explain
                                                                                                  // very well the
                                                                                                  // choices
{
    // ROS_INFO("Received request for vacancy check for "); //TODO(minor) complete

    /* If the request for vacancy check is not about the target DS of the robot,
     * for sure the robot is not occupying it
     */
    if (target_ds != NULL && msg.get()->id == target_ds->id)  // TODO at the beginning no robot has
                                                              // already target_ds set, since it is set
                                                              // only after the end of all auctions!!!!!

        /* If the robot is going to or already charging, or if it is going to check
         * already checking for vacancy, it is
         * (or may be, or will be) occupying the DS */
        if (robot_state == charging || robot_state == going_charging || robot_state == going_checking_vacancy ||
            robot_state == checking_vacancy || robot_state == fully_charged || robot_state == leaving_ds)
        {
            /* Print some debut text */
            if (robot_state == charging || robot_state == going_charging)
                ROS_ERROR("\n\t\e[1;34mI'm using / going to use that DS!!!!\e[0m");
            else if (robot_state == going_checking_vacancy || robot_state == checking_vacancy)
                ROS_ERROR("\n\t\e[1;34mI'm approachign that DS too!!!!\e[0m");
            else if (robot_state == fully_charged || robot_state == leaving_ds)
                ROS_ERROR("\n\t\e[1;34mI'm leaving the DS, jsut wait a sec...\e[0m");

            /* Reply to the robot that asked for the check, telling it that the DS is
             * occupied */
            adhoc_communication::SendEmDockingStation srv_msg;
            srv_msg.request.topic = "explorer/adhoc_communication/reply_for_vacancy";
            srv_msg.request.docking_station.id = target_ds->id;
            sc_send_docking_station.call(srv_msg);
        }
        else
            ROS_DEBUG("\n\t\e[1;34m target ds, but currently not used by the robot \e[0m");
    else
        ROS_DEBUG("\n\t\e[1;34m robot is not targetting that ds\e[0m");
}

void docking::update_robot_state()  // TODO(minor) simplify
{
    /*
     * Check if:
     * - there are no more pending auctions: this is to avoid to communicate
     *contradicting information to the explorer
     *   node about the next state of the robot, so it is better to wait that all
     *the auctions have been finished and
     *   then make a "global analisys" of the sistuation;
     * - an update of the state is required, i.e., that at least one auction was
     *recently performed and that the robot
     *   took part to it (i.e., that it placed a bid).
     *
     * Notice that it may happen that the energy_mgmt node thinks that an update
     *of the state is required, but maybe it
     * is not necessary: it is the explorer node that has to make some ckecks and,
     *only if necessary, update the state;
     * the energy_mgmt node just informs the explorer node that something has
     *recently happened.
     */
    if (update_state_required && participating_to_auction == 0)
    {
        /* An update of the robot state is required and it can be performed now */
        ROS_INFO("Sending information to explorer node about the result of recent "
                 "auctions");

        /* Create the empty message to be sent */
        std_msgs::Empty msg;

        /* If the robot is not the winner of the most recent auction, notify
         * explorer.
         * Notice that for sure it took part to at least one auction, or
         * 'update_state_required' would be false and we
         * wouldn't be in this then-branch */
        if (!auction_winner)
        {
            if (started_own_auction)  // TODO should be better to use a variqable that
                                      // keep track of teh fact that the robot started
                                      // its own auction, since if !auction_winner is
                                      // true, it is already enough to know that the
                                      // robot needs to recharge (i.e., lost its own
                                      // auction)...
                                      /* Notify explorer node about the lost of an auction started by the
                                       * robot itself */
                pub_lost_own_auction.publish(msg);

            /* If the robot has lost an auction that was not started by it, notify
             * explorer (because if the robot was
             * recharging, it has to leave the docking station) */
            else
                pub_lost_other_robot_auction.publish(msg);
        }

        /* Robot is the winner of at least one auction, notify explorer */
        else
        {
            /* If the robot is already approaching a DS to recharge, if it is already
             *charging, etc., ignore the fact
             *that meanwhile it has won another auction.
             *
             * This check (i.e., the if condition) is necessary because, while moving
             *toward the DS, the robot could
             *have changed its currently optimal DS and it could have also taken part
             *to an auction for this new DS and
             *won it; without the check the explorer node would be notified for a
             *change of target DS even if the robot
             *is still approaching the old one, which could cause problems when the
             *robot state changes from
             *checking_vacancy to going_charging, since it would move to the new DS
             *without doing again the vacancy
             *check (this is because the move_robot() function in explorer node uses
             *the coordinates of the currently
             *set target DS when the robot wants to reach a DS). */
            if (robot_state != charging || robot_state != going_charging || robot_state != going_checking_vacancy ||
                robot_state != checking_vacancy)
            {
                /* Notify explorer node about the new target DS.
                 * Notice that it is important that target_ds is updated only here,
                 * because otherwise there could be problem when the robot communicates
                 * to the other robot that the DS that is currently targettting is now
                 * vacant/occupied. */
                target_ds = best_ds;
                geometry_msgs::PointStamped msg1;
                msg1.point.x = target_ds->x;
                msg1.point.y = target_ds->y;
                pub_new_target_ds.publish(msg1);

                /* Notify explorer node about the victory */
                pub_won_auction.publish(msg);  // TODO it is important that this is after the other pub!!!!
            }
            else
                ROS_ERROR("The robot has already won an auction: ignore the result of "
                          "the most recent auction");
        }

        /* Reset all the variables that are used to keep information about the
         * auctions results (i.e., about the next
         * robot state) */
        update_state_required = false;
        auction_winner = false;
        started_own_auction = false;
        timers.clear();  // TODO(minor) inefficient!!
    }
    else
    {
        /* Do nothing, just print some debug text */
        if (participating_to_auction > 0 && !update_state_required)
            ROS_DEBUG("There are still pending auctions, and moreover no update is "
                      "necessary for the moment");
        else if (!update_state_required)
            ROS_DEBUG("No state update required");
        else if (participating_to_auction > 0)
            ROS_DEBUG("There are still pending auctions, cannot update robot state");
        else
            ROS_FATAL("ERROR: the number of pending auctions is negative: %d", participating_to_auction);
    }
}

void docking::create_log_files()
{
    ROS_INFO("Creating log files...");

    /* Create directory */
    log_path = log_path.append("/energy_mgmt");
    log_path = log_path.append(robot_name);
    boost::filesystem::path boost_log_path(log_path.c_str());
    if (!boost::filesystem::exists(boost_log_path))
    {
        ROS_INFO("Creating directory %s", log_path.c_str());
        try
        {
            if (!boost::filesystem::create_directories(boost_log_path))
            {
                ROS_ERROR("Cannot create directory %s: aborting node...", log_path.c_str());
                exit(-1);
            }
        }
        catch (const boost::filesystem::filesystem_error &e)
        {
            ROS_ERROR("Cannot create path %saborting node...", log_path.c_str());
            exit(-1);
        }
    }
    else
    {
        ROS_INFO("Directory %s already exists: log files will be saved there", log_path.c_str());
    }

    /* Create file names */
    log_path = log_path.append("/");
    csv_file = log_path + std::string("update_ds.log");
    csv_file_2 = log_path + std::string("periodical.log");
    csv_file_3 = log_path + std::string("received_info.log");
    info_file = log_path + std::string("info.csv");

    /* Create and initialize files */
    fs_csv.open(csv_file.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    fs_csv << "#time,optimal_ds,num_robots,ds_selection_policy,start_absolute_x,"
              "starting_absolute_y" << std::endl;
    fs_csv.close();

    fs2_csv.open(csv_file_2.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    fs2_csv << "#time,x,y,num_robots,ds_selection_policy,start_absolute_x,"
               "starting_absolute_y" << std::endl;
    fs2_csv.close();

    fs3_csv.open(csv_file_3.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    fs3_csv << "#time,sender_robot_id,num_robots,ds_selection_policy,start_"
               "absolute_x,starting_absolute_y" << std::endl;
    fs3_csv.close();

    fs_info.open(info_file.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    fs_info << "#robot_id,num_robots,ds_selection_policy,start_absolute_x,"
               "starting_absolute_y" << std::endl;
    fs_info << robot_id << "," << num_robots << "," << ds_selection_policy << "," << origin_absolute_x << ","
            << origin_absolute_y << std::endl;
    fs_info.close();
}

void docking::set_target_ds_vacant(bool vacant)
{
    adhoc_communication::SendEmDockingStation srv_msg;
    srv_msg.request.topic = "docking_stations";
    srv_msg.request.docking_station.id = target_ds->id;
    double x, y;
    rel_to_abs(target_ds->x, target_ds->y, &x, &y);
    srv_msg.request.docking_station.x = x;  // it is necessary to fill also this fields because when a Ds is
                                            // received, robots perform checks on the coordinates
    srv_msg.request.docking_station.y = y;
    srv_msg.request.docking_station.vacant = vacant;
    sc_send_docking_station.call(srv_msg);

    target_ds->vacant = vacant;

    ROS_ERROR("ds%d: %d", target_ds->id, target_ds->vacant);

    update_l1();
}

// clang-format off
void docking::compute_MST(std::vector<std::vector<int> > graph)  // TODO(minor) check all functions related to MST
// clang-format on
{
    int V = graph.size();
    int parent[V];   // Array to store constructed MST
    int key[V];      // Key values used to pick minimum weight edge in cut
    bool mstSet[V];  // To represent set of vertices not yet included in MST

    // Initialize all keys as INFINITE
    for (int i = 0; i < V; i++)
        key[i] = INT_MAX, mstSet[i] = false;

    // Always include first 1st vertex in MST.
    key[0] = 0;      // Make key 0 so that this vertex is picked as first vertex
    parent[0] = -1;  // First node is always root of MST

    // The MST will have V vertices
    for (int count = 0; count < V - 1; count++)
    {
        // Pick the minimum key vertex from the set of vertices
        // not yet included in MST
        int u = minKey(key, mstSet, V);

        // Add the picked vertex to the MST Set
        mstSet[u] = true;

        // Update key value and parent index of the adjacent vertices of
        // the picked vertex. Consider only those vertices which are not yet
        // included in MST
        for (int v = 0; v < V; v++)

            // graph[u][v] is non zero only for adjacent vertices of m
            // mstSet[v] is false for vertices not yet included in MST
            // Update the key only if graph[u][v] is smaller than key[v]
            if (graph[u][v] && mstSet[v] == false && graph[u][v] < key[v])
                parent[v] = u, key[v] = graph[u][v];
    }

    // print the constructed MST
    // printMST(parent, V, graph);

    for (int i = 0; i < V; i++)
        for (int j = 0; j < V; j++)
            ds_mst[i][j] = 0;

    // TODO does not work if a DS is not connected to any other DS
    for (int i = 1; i < V; i++)
    {
        ds_mst[i][parent[i]] = 1;  // parent[i] is the node closest to node i
        ds_mst[parent[i]][i] = 1;
    }

    /*
    for (int i = 0; i < V; i++)
        for (int j = 0; j < V; j++)
            ROS_ERROR("(%d, %d): %d ", i, j, ds_mst[i][j]);
     */

    /*
    int k = 0;              // index of the closest recheable DS
    int target = 4;         // the id of the DS that we want to reach
    std::vector<int> path;  // sequence of nodes that from target leads to k;
    i.e., if the vector is traversed in the
                            // inverse order (from end to begin), it contains the
    path to go from k to target

    find_path(ds_mst, k, target, path);

    std::vector<int>::iterator it;
    for (it = path.begin(); it != path.end(); it++)
        ROS_ERROR("%d - ", *it);
        */
}

// A utility function to find the vertex with minimum key value, from
// the set of vertices not yet included in MST
int docking::minKey(int key[], bool mstSet[], int V)
{
    // Initialize min value
    int min = INT_MAX, min_index;

    for (int v = 0; v < V; v++)
        if (mstSet[v] == false && key[v] < min)
            min = key[v], min_index = v;

    return min_index;
}

// A utility function to print the constructed MST stored in parent[]
int docking::printMST(int parent[], int n, std::vector<std::vector<int> > graph)
{
    printf("Edge   Weight\n");
    for (int i = 1; i < graph.size(); i++)
        ROS_ERROR("%d - %d    %d \n", parent[i], i, graph[i][parent[i]]);
}

bool docking::find_path(std::vector<std::vector<int> > tree, int start, int end, std::vector<int> &path)
{
    /* Temporary variable to store the path from 'end' to 'start' */
    std::vector<int> inverse_path;

    /* Call auxiliary function find_path_aux to perform the search */
    bool path_found = find_path_aux(tree, start, end, inverse_path, start);

    // TODO bad doing it here...
    inverse_path.push_back(start);

    /* If a path was found, reverse the constructed path to obtain the real path
     * that goes from 'start' to 'end' */
    if (path_found)
        for (int i = inverse_path.size() - 1; i >= 0; i--)
            path.push_back(inverse_path.at(i));

    /* Tell the caller if a path was found or not */
    return path_found;
}

bool docking::find_path_aux(std::vector<std::vector<int> > tree, int start, int end, std::vector<int> &path,
                            int prev_node)
{
    /* Loop on all tree nodes */
    for (int j = 0; j < tree.size(); j++)

        /* Check if there is an edge from node 'start' to node 'j', and check if
         * node 'j' is not the node just visited
         * in the previous step of the recursive descending */
        if (tree[start][j] > 0 && j != prev_node)

            /* If 'j' is the searched node ('end'), or if from 'j' there is a path
             * that leads to 'end', we have to store
             * 'j' as one of the node that must be visited to reach 'end' ('j' could
             * be 'end' itself, but of course this
             * is not a problem, since we have to visit 'end' to reach it), and then
             * return true to tell the caller that
             * a path to 'end' was found */
            if (j == end || find_path_aux(tree, j, end, path, start))
            {
                path.push_back(j);
                return true;
            }

    /* If, even after considering all the nodes of the tree, we have not found a
     * node that is connected to 'start' and
     * from which it is possible to find a path leading to 'end', it means that no
     * path from 'start' to 'end' exists:
     * return false */
    return false;
}

void docking::compute_closest_ds()
{
    double min_dist = numeric_limits<int>::max(), dist;
    std::vector<ds_t>::iterator it;
    for (it = ds.begin(); it != ds.end(); it++)
    {
        dist = distance_from_robot((*it).x, (*it).y);
        if (dist < 0)
            continue;
        if (dist < min_dist)
        {
            min_dist = dist;
            best_ds = &(*it);
        }
    }
}

void docking::discover_docking_stations()
{
    /* Get current robot position (once the service required to do that is ready)
     */ //TODO(minor) reduntant...
    ros::service::waitForService("explorer/robot_pose");
    explorer::RobotPosition srv_msg;
    if (sc_robot_pose.call(srv_msg))
    {
        robot.x = srv_msg.response.x;
        robot.y = srv_msg.response.y;
        ROS_DEBUG("Robot position: (%f, %f)", robot.x, robot.y);
    }
    else
    {
        ROS_ERROR("Call to service %s failed; not possible to compute optimal DS "
                  "for the moment",
                  sc_robot_pose.getService().c_str());
        return;
    }

    /* Check if there are DSs that can be considered discovered */

    for (std::vector<ds_t>::iterator it = undiscovered_ds.begin(); it != undiscovered_ds.end(); it++)
    {
        /* Compute distance from robot */
        double dist = distance_from_robot((*it).x, (*it).y);

        if (dist > 0 && dist < fiducial_signal_range)  // TODO
        {
            /* New DS discovered */
            ROS_INFO("Discovered");

            /* Store new DS in the vector of known DSs, and remove it from the vector
             * of undiscovered DSs */
            ROS_INFO("Found new DS ds%d at (%f, %f)", (*it).id, (*it).x,
                     (*it).y);  // TODO index make sense only in simulation
            discovered_ds.push_back(*it);
            undiscovered_ds.erase(it);

            /* Inform other robots about the "new" DS */
            adhoc_communication::SendEmDockingStation send_ds_srv_msg;
            send_ds_srv_msg.request.topic = "docking_stations";
            send_ds_srv_msg.request.docking_station.id = (*it).id;
            double x, y;
            rel_to_abs((*it).x, (*it).y, &x, &y);
            send_ds_srv_msg.request.docking_station.x = x;
            send_ds_srv_msg.request.docking_station.y = y;
            send_ds_srv_msg.request.docking_station.vacant = true;  // TODO sure???

            /* Since an element from 'undiscovered_ds' was removed, we have to
             * decrease the iterator by one to
             * compensate the future increment of the for loop, since, after the
             * deletion of the element, all the
             * elements are shifted by one position, and so we are already pointing to
             * the next element, even without
             * the future increment of the for loop */
            it--;
        }
    }
}

void docking::send_robot()
{
    if (robot_id == 0 && DEBUG)
    {
        // ROS_ERROR("%f", distance(robot.x, robot.y, -0.5, -1));
        ROS_ERROR("(%f, %f)", robot.x, robot.y);
        ROS_ERROR("%f", distance(robot.x, robot.y, 0, 0));
        ROS_ERROR("%f", distance(robot.x, robot.y, 0, 0, true));
    }
    adhoc_communication::SendEmRobot robot_msg;
    robot_msg.request.topic = "robots";
    robot_msg.request.robot.id = robot_id;
    robot_msg.request.robot.x = robot.x;  // TODO robot->x ??? and is this robot in robots????
    robot_msg.request.robot.y = robot.y;
    robot_msg.request.robot.state = robot.state;
    if (best_ds != NULL)
        robot_msg.request.robot.selected_ds = best_ds->id;
    else
        robot_msg.request.robot.selected_ds = -1;
    sc_send_robot.call(robot_msg);

    ros::Duration time = ros::Time::now() - time_start;
    fs2_csv.open(csv_file_2.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    fs2_csv << time.toSec() << "," << robot.x << "," << robot.y << "," << num_robots << "," << ds_selection_policy
            << "," << origin_absolute_x << "," << origin_absolute_y << std::endl;
    fs2_csv.close();
}

void docking::send_fake_msg()
{
    adhoc_communication::SendEmRobot robot_msg;
    robot_msg.request.topic = "robots";
    robot_msg.request.robot.id = robot_id;
    robot_msg.request.robot.x = robot_id;
    robot_msg.request.robot.y = robot_id;
    robot_msg.request.robot.state = robot.state;
    robot_msg.request.robot.selected_ds = robot_id;
    sc_send_robot.call(robot_msg);
}

void docking::debug_timer_callback_0(const ros::TimerEvent &event)
{
    ROS_ERROR("No information received by robot 0 for a certain amount of time!!!");
}

void docking::debug_timer_callback_1(const ros::TimerEvent &event)
{
    ROS_ERROR("No information received by robot 1 for a certain amount of time!!!");
}

void docking::debug_timer_callback_2(const ros::TimerEvent &event)
{
    ROS_ERROR("No information received by robot 2 for a certain amount of time!!!");
}

void docking::next_ds_callback(const std_msgs::Empty &msg)
{
    ROS_ERROR("next_ds_callback");
    if (index_of_ds_in_path < path.size() - 1)
    {
        index_of_ds_in_path++;
        for (int i = 0; i < ds.size(); i++)
            if (path[index_of_ds_in_path] == ds[i].id)
            {
                target_ds = &ds[i];  // TODO probably ok...
                best_ds = &ds[i];    // TODO VERY BAD!!!!
                break;
            }
    }
    else
        moving_along_path = false;
}

void docking::check_reachable_ds()
{
    bool new_ds_discovered = false;
    for (std::vector<ds_t>::iterator it = discovered_ds.begin(); it != discovered_ds.end(); it++)
    {
        /* If the DS is inside a fiducial laser range, it can be considered
         * discovered */
        bool reachable;
        explorer::DistanceFromRobot srv_msg;
        srv_msg.request.x = (*it).x;
        srv_msg.request.y = (*it).y;
        ros::service::waitForService("explorer/reachable_target");
        if (sc_reachable_target.call(srv_msg))
            reachable = srv_msg.response.reachable;
        else
        {
            ROS_ERROR("Unable at the moment...");
            return;
        }

        if (reachable)
        {
            ROS_INFO("Reachable DS");
            new_ds_discovered = true;
            ds.push_back(*it);
            discovered_ds.erase(it);
            it--;
        }
        else
            ROS_INFO("UNREACHABLE!!!");
    }

    if (new_ds_discovered || recompute_graph)
    {
        // construct ds graph //TODO construct graph only when a new DS is found
        for (int i = 0; i < ds.size(); i++)
            for (int j = 0; j < ds.size(); j++)
                if (i == j)
                    ds_graph[i][j] = 0;
                else
                {
                    double dist;
                    // while(dist == -1) //TODO
                    dist = distance(ds.at(i).x, ds.at(i).y, ds.at(j).x, ds.at(j).y);
                    if (dist < 0)
                    {
                        recompute_graph = true;
                        return;
                    }
                    if (dist < battery.remaining_distance / 2)
                    {
                        ds_graph[i][j] = dist;
                        ds_graph[j][i] = dist;
                    }
                    else
                    {
                        ds_graph[i][j] = 0;
                        ds_graph[j][i] = 0;
                    }
                }

        recompute_graph = false;

        // construct MST starting from ds graph
        compute_MST(ds_graph);
    }
}

void docking::finalize()
{
    ros::Duration time = ros::Time::now() - time_start;

    fs_csv.open(csv_file.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    fs_csv << time.toSec() << ","
           << "-1"
           << "," << num_robots << "," << ds_selection_policy << "," << origin_absolute_x << "," << origin_absolute_y
           << std::endl;
    fs_csv << "#end";
    fs_csv.close();

    fs3_csv.open(csv_file_3.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    fs3_csv << time.toSec() << ","
            << "-1"
            << "," << num_robots << "," << ds_selection_policy << "," << origin_absolute_x << "," << origin_absolute_y
            << std::endl;
    fs3_csv << "#end";
    fs3_csv.close();

    fs2_csv.open(csv_file_2.c_str(), std::fstream::in | std::fstream::app | std::fstream::out);
    // fs2_csv << time.toSec() << "," << origin_absolute_x << "," <<
    // origin_absolute_x << "," << num_robots << "," << ds_selection_policy << ","
    // << origin_absolute_x << "," << origin_absolute_y << std::endl; //TODO do
    // better...
    fs2_csv << "#end";
    fs2_csv.close();

    ros::Duration(5).sleep();

    // exit(0);
}

int docking::next_auction_id()
{
    local_auction_id++;
    auction_id = local_auction_id * pow(10, (ceil(log10(num_robots)))) + robot_id;
    return auction_id;
}

void docking::spin()
{
    
    while(ros::ok) {
        ros::Duration(0.1);
        ros::spinOnce();
    }
}

void docking::update_robot_position()
{
    /* Get current robot position (once the service required to do that is ready)
     */
    ros::service::waitForService("explorer/robot_pose");  // TODO(minor)
    explorer::RobotPosition srv_msg;
    if (sc_robot_pose.call(srv_msg))
    {
        robot.x = srv_msg.response.x;
        robot.y = srv_msg.response.y;
        ROS_DEBUG("Robot position: (%f, %f)", robot.x, robot.y);
    }
    else
    {
        ROS_ERROR("Call to service %s failed; not possible to update robot position for the moment",
                  sc_robot_pose.getService().c_str());
        return;
    }
}