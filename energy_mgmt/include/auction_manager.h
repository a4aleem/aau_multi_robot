#ifndef AUCTION_MANAGER_H
#define AUCTION_MANAGER_H

#include "auction_manager_interface.h"

class AuctionManager : public AuctionManagerInterface {
public:
    AuctionManager(unsigned int robot_id);
    void setBidComputer(BidComputer *bid_computer);
    void setTimeManager(TimeManagerInterface *time_manager);
    void setSender(Sender *sender);

    void tryToAcquireDs();
    bool isRobotParticipatingToAuction();
    bool isRobotWinnerOfMostRecentAuction();
    void preventParticipationToAuctions();
    void allowParticipationToAuctions();
    void setOptimalDs(unsigned int optimal_ds_id);
    auction_t getCurrentAuction();
    void lock();
    void unlock();

    unsigned int _u1, _u2, _u3, _u4, _u5;
    bool _b1, _b2, _b3, _b4;
    int _d1;

private:
    BidComputer *bid_computer;
    TimeManagerInterface *time_manager;
    Sender *sender;
    double auction_timeout, reauctioning_timeout, extra_auction_time;
    auction_participation_state_t auction_participation_state;
    bool winner_of_auction;
    boost::mutex auction_mutex;
    ros::NodeHandle nh;
    ros::Timer terminate_auction_timer, timer_restart_auction;
    unsigned int optimal_ds_id;
    unsigned int robot_id;
    std::vector<bid_t> auction_bids;
    ros::Subscriber auction_result_sub, auction_reply_sub, auction_starting_sub;
    ros::ServiceClient sc_send_auction;
    auction_t current_auction;
    bool robot_cannot_participate_to_auctions;
    bool optimal_ds_is_set;
    double sleep_time_between_two_participations;
    double time_last_participation;
    unsigned int local_auction_id;
    unsigned int num_robots;
    std::string auction_starting_topic, auction_reply_topic, auction_result_topic;

    void initializeVariables(unsigned int robot_id);
    void loadParameters();
    void createSubscribers();
    void createServiceClients();
    auction_t startNewAuction();
    unsigned int nextAuctionId();
    void scheduleAuctionTermination();
//    void sendBid(bid_t bid, std::string topic);
    void terminateAuctionCallback(const ros::TimerEvent &event);
    unsigned int computeAuctionWinner();
    bool isThisRobotTheWinner(unsigned int winner_id);
    void sendAuctionResult(bid_t bid);
    void auctionReplyCallback(const adhoc_communication::EmAuction::ConstPtr &msg);
    void auctionStartingCallback(const adhoc_communication::EmAuction::ConstPtr &msg);
    void auctionResultCallback(const adhoc_communication::EmAuction::ConstPtr &msg);
    void endAuctionParticipationCallback(const ros::TimerEvent &event);
    void restartAuctionCallback(const ros::TimerEvent &event);
    auction_t participateToOtherRobotAuction(double bid_double, const adhoc_communication::EmAuction::ConstPtr &msg);
};

#endif // AUCTION_MANAGER_H