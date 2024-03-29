/******************************************************
 * Project:         Práctica 3 de Sistemas Operativos II
 * 
 * Program name:    cinemaSale.cpp
 
 * Author:          María Espinosa Astilleros
 * 
 * Date created:    2/4/2020
 * 
 * Purpose:         It simulates a sales system online in cinemas
 * 
 ******************************************************/

#include <iostream>
#include <queue>
#include <vector>
#include <mutex> 
#include <thread> 
#include <condition_variable> 
#include <chrono> 
#include <csignal>
#include <string> 
#include <signal.h>
#include <unistd.h>

#include "../include/color.h"
#include "../include/msgRequest.h"
#include "../include/SemCounter.h"

#define NUM_SEATS               72
#define NUM_SP                  3
#define NUM_CLIENTS             30
#define MAX_REQUEST_TICKETS     6
#define MAX_REQUEST_DRINK_POP   10
#define PAY_TO                  1 
#define PAY_SP                  2 

/*Struct*/
struct InfoSalePoint {
	int id;              /*id of sale point*/
	int num_drinks;      /*quantity of drink that the point of sale has*/
	int num_popcorn;     /*quantity of popcorn that the point of sale has*/
	int num_replenish;   /*quantity of drinks and popcorn the sale point replenishes*/
};

/*Globals variables*/
int g_turn_tickets = 0;
int g_turn_food    = 0; 
int g_num_seats    = NUM_SEATS;

/*Messages queue*/
std::queue<std::thread>                 g_queue_tickets;            /*queue of clients to buy tickets*/
std::queue<std::thread>                 g_queue_clients_out;        /*queue of clients that not buy tickets*/
std::queue<std::thread>                 g_queue_cinema;             /*queue representing cinema*/
std::queue<std::thread>                 g_queue_drinkpop;           /*queue where the client go to inside of cinema to buy drinks and popcorn*/
std::queue<MsgRequestTickets*>          g_queue_request_tickets;    /*queue to request tickets*/
std::queue<MsgRequestSalePoint*>        g_queue_request_sp;         /*queue to request sale point*/
std::queue<InfoSalePoint*>              g_queue_request_stock;      /*queue to request thread stocker*/
std::priority_queue<MsgRequestPayment*> g_queue_request_payment;    /*queue to request pay*/

/*Semaphores*/
SemCounter                              g_sem_seats(1);             /*sem to control seats*/
SemCounter                              g_sem_payment(1);           /*sem to control payment*/
SemCounter                              g_sem_replenisher(1);       /*sem to control replenisher*/
SemCounter                              g_sem_sale_point(0);        /*sem to control sale point*/
std::mutex                              g_sem_tickets;              /*sem to wait tickets*/
std::mutex                              g_sem_toffice;              /*sem to wake ticket office*/
std::mutex                              g_sem_manager_tickets;      /*sem to manager send a new turn in ticket office*/
std::mutex                              g_sem_drink_popcorn;        /*sem to wait drinks and popcorns*/
std::mutex                              g_sem_turn_tickets;         /*sem to control the turn in ticket office*/
std::mutex                              g_sem_turn_food;            /*sem to control the turn in sale point*/
std::mutex                              g_sem_mutex_access_payment; /*sem to control the access to payment request queue*/
std::mutex                              g_sem_mutex_access_sp;      /*sem to control the access to sale points request queue*/
std::mutex                              g_sem_mutex_payment;        /*sem to control section critical in payment system*/
std::mutex                              g_sem_wait_payment;         /*sem to wait confirmation of tickets payment*/

/*Condition variable*/
std::condition_variable                 g_cv_ticket_office;         /*condition variable to notify the turn of ticket office*/
std::condition_variable                 g_cv_drinks_popcorn;        /*condition variable to notify the turn of buy drinks and popcorn*/
std::condition_variable                 g_cv_receive_food;          /*condition variable to notify that the client has received drinks and popcorn*/
std::condition_variable                 g_cv_payment;               /*condition variable to notify if the client has paid tickets*/

/*Functions declaration*/
int                  generateRandomNumber(int lim); 
void                 signalHandler(int signal); 
void                 messageWelcome(); 
void                 showInfo(); 
void                 blockSem();
int                  priorityAssignment();
void                 createClients();  
void                 client(int id_client); 
MsgRequestTickets    buyTickets(int id_client);
void                 checkTicketsClient(int id_client, MsgRequestTickets mrt);
void                 ticketOffice();
void                 checkNumTickets(MsgRequestTickets *mrt);
void                 buyDrinksPopcorn(int id_client);
void                 checkPaymentTicketOffice(MsgRequestPayment mrp, MsgRequestTickets *mrt); 
void                 salePoint(InfoSalePoint &sp); 
void                 checkNumDrinksPopcorn(MsgRequestSalePoint *mrsp, InfoSalePoint &sp);
void                 requestReplenisher(MsgRequestSalePoint *mrsp, InfoSalePoint &sp);
void                 checkPaymentSalePoint(MsgRequestSalePoint *mrsp, InfoSalePoint &sp);
void                 replenish();
void                 paymentSystem();
void                 manager(); 

/******************************************************
 * Function name:    generateRandomNumber
 * Date created:     4/4/2020
 * Input arguments: 
 * Purpose:          Generate a random number 
 * 
 ******************************************************/
int generateRandomNumber(int lim){ return (rand()%(lim-1))+1; }

/******************************************************
 * Function name:    signalHandler
 * Date created:     11/4/2020
 * Input arguments: 
 * Purpose:          Signal Handler to show a message when the user uses CTRL + C 
 * 
 ******************************************************/
void signalHandler(int signal){
    std::cout << BOLDWHITE << "[HANDLER] It has been received the signal CTRL+C. The program ended...\n" << RESET << std::endl; 
    kill(getpid(), SIGKILL); 
    std::exit(EXIT_SUCCESS); 
}

/******************************************************
 * Function name:    messageWelcome
 * Date created:     12/4/2020
 * Input arguments: 
 * Purpose:          Show a message welcome
 * 
 ******************************************************/
void messageWelcome(){
    std::cout << CURVCYAN << "*********************************************" << RESET << std::endl;
    std::cout << CURVCYAN << "* WELCOME TO SALES SYSTEM ONLINE IN CINEMAS *" << RESET << std::endl; 
    std::cout << CURVCYAN << "*********************************************" << RESET << std::endl;
}

/******************************************************
 * Function name:    blockSem
 * Date created:     16/4/2020
 * Input arguments:  
 * Purpose:          Block semaphores 
 * 
 ******************************************************/
void blockSem(){
    g_sem_tickets.lock();             
    g_sem_toffice.lock();                            
    g_sem_manager_tickets.lock();    
    g_sem_sale_point.wait();             
    g_sem_payment.wait(); 
    g_sem_replenisher.wait();    
}

/******************************************************
 * Function name:    priorityAssignment
 * Date created:     16/4/2020
 * Input arguments:  
 * Purpose:          Assign priority to clients when paying.
 * 
 ******************************************************/
int priorityAssignment(int type_payment){
    int priority     = 0; 
    int num_random   = drand48(); 

    switch (type_payment){
        case PAY_TO:
            if(num_random < 0.8){ 
                priority = 1;
            }else{
                priority = 2;
            }
            break;
    
        case PAY_SP:
            if(num_random > 0.8){
                priority = 1;
            }else{
                priority = 2; 
            }
            break;
    }
    return priority; 
}

/******************************************************
 * Function name:    createClients
 * Date created:     11/4/2020
 * Input arguments:  
 * Purpose:          Create the clients 
 * 
 ******************************************************/
void createClients(){
    for(int i = 1; i <= NUM_CLIENTS; i++){
        g_queue_tickets.push(std::thread(client, i));
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
    }
}

/******************************************************
 * Function name:    client
 * Date created:     11/4/2020
 * Input arguments:  id of client 
 * Purpose:          It simulate the client. The client go to the ticket office and when the client has the turn send the request of tickets.
 *                   When the ticket office attends the request, the client waits. If there are sufficient tickets the client 
 *                   come in the cinema and buy drinks and popcorn
 * 
 ******************************************************/
void client(int id_client){
    std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] Created and waiting to buy tickets..." << RESET << std::endl;
    MsgRequestTickets mrt = buyTickets(id_client); 
    checkTicketsClient(id_client, mrt); 
}

/******************************************************
 * Function name:    buyTickets
 * Date created:     12/4/2020
 * Input arguments:  
 * Purpose:          The client buys tickets
 * 
 ******************************************************/
MsgRequestTickets buyTickets(int id_client){
    /*Wait turn of office ticket*/
    std::unique_lock<std::mutex> ul_turn_ticket(g_sem_turn_tickets); 
        g_cv_ticket_office.wait(ul_turn_ticket, [id_client]{return g_turn_tickets == id_client;}); 
        std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] It's my turn for buy tickets!" << RESET << std::endl; 
    ul_turn_ticket.unlock(); 

    /*Generate the request to buy a tickets*/
    MsgRequestTickets mrt(id_client, generateRandomNumber(MAX_REQUEST_TICKETS));
    g_queue_request_tickets.push(&mrt); 
    std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] I want " << std::to_string(mrt.num_seats) << " tickets" << RESET << std::endl; 

    /*Unlocked the ticket office and wait to receive tickets*/
    g_sem_toffice.unlock(); 
    g_sem_tickets.lock(); 

    return mrt;
}

/******************************************************
 * Function name:    checkTicketsClient
 * Date created:     12/4/2020
 * Input arguments:  
 * Purpose:          Check if there are enough tickets for the client. If the client has paid for the tickets,
 *                   he will buy drinks and popcorn. If there are not enough tickets the client leaves the cinema.
 * 
 ******************************************************/
void checkTicketsClient(int id_client, MsgRequestTickets mrt){
    /*Check it the client has sufficient seats and it can buy drinks and popcorn*/
    if(mrt.suff_seats == true){
        /*The client goes inside the cinema*/
        std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] I have the tickets already. I go to buy drinks and popcorn..." << RESET << std::endl; 
        g_sem_manager_tickets.unlock(); /*It unlocks the turn to the next client sends the request*/
        g_queue_drinkpop.push(std::move(g_queue_tickets.front()));
        g_queue_tickets.pop(); 
        std::this_thread::sleep_for(std::chrono::milliseconds(400));

        /*The client buys drinks and popcorn*/
        buyDrinksPopcorn(id_client); 
        std::this_thread::sleep_for(std::chrono::milliseconds(600)); 
        std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] I have everything already. I go to see Harry Potter now! :)" << RESET << std::endl;
    }else{
        g_queue_clients_out.push(std::move(g_queue_tickets.front()));
        g_queue_tickets.pop();
        std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] No tickets left so I go to my house :(" << RESET << std::endl;
        g_sem_manager_tickets.unlock(); /*It unlocks the turn to the next client sends the request*/
    }
}

/******************************************************
 * Function name:    ticketOffice
 * Date created:     12/4/2020
 * Input arguments:  
 * Purpose:          It simulate the ticket office. The sale point checks if there are enough tickets 
 *                   for the customer. If there is then give the tickets to the customer and ask for the request for payment. 
 * 
 ******************************************************/
void ticketOffice(){
    std::cout << GREEN << "[TICKET OFFICE] Ticket office open" << RESET << std::endl; 
    while(true){
        try{
            g_sem_toffice.lock(); 
            MsgRequestTickets *mrt = g_queue_request_tickets.front(); 
            g_queue_request_tickets.pop(); 

            /*Check number of tickets*/
            checkNumTickets(mrt);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            std::cout << GREEN << "[TICKET OFFICE] The client " << std::to_string(mrt->id_client) << " has been attended" << RESET << std::endl;
            g_sem_tickets.unlock(); /*It unlocks to attend other clients*/ 
            
        }catch(std::exception &e){
            std::cout << GREEN << "[TICKET OFFICE] An error occurred while attending clients..." << RESET << std::endl;
            g_sem_tickets.unlock();
        }
    }
}

/******************************************************
 * Function name:    checkNumTickets
 * Date created:     22/4/2020
 * Input arguments:  
 * Purpose:          Check tickets  
 * 
 ******************************************************/
void checkNumTickets(MsgRequestTickets *mrt){
    if(g_num_seats >= mrt->num_seats){
        std::cout << GREEN << "[TICKET OFFICE] The client " << mrt->id_client << " has requested " << mrt->num_seats  << " tickets"<< RESET << std::endl; 

        g_sem_mutex_payment.lock();
        MsgRequestPayment mrp(mrt->id_client, priorityAssignment(PAY_TO));
        g_queue_request_payment.push(&mrp);
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); /*sleep the thread each time that the client pays tickets*/
        std::cout << GREEN << "[TICKET OFFICE] I request the client's payment"<< RESET << std::endl; 
        /*Wait confirmation of payment system*/
        std::unique_lock<std::mutex> ul_wait_payment(g_sem_wait_payment); 
            /*Seat allocation and payment system simultaneous*/ 
            g_sem_payment.signal();  
            g_sem_seats.signal();
            bool *p_flag_attended = &(mrp.attended);
            g_cv_payment.wait(ul_wait_payment, [p_flag_attended] {return *p_flag_attended;});  
            g_sem_mutex_payment.unlock();
        ul_wait_payment.unlock();

        /*Check if the payment was successful*/
        checkPaymentTicketOffice(mrp, mrt);
    }else{
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << GREEN << "[TICKET OFFICE] The client " << std::to_string(mrt->id_client) << " has requested more tickets than there are left" << RESET << std::endl;
        mrt->suff_seats = false; 
    }
}

/******************************************************
 * Function name:    checkPaymentTicketOffice
 * Date created:     22/4/2020
 * Input arguments:  
 * Purpose:          Check if the payment was successful 
 * 
 ******************************************************/
void checkPaymentTicketOffice(MsgRequestPayment mrp, MsgRequestTickets *mrt){
    if(mrp.attended == true){ 
        /*Updated the number of tickets left*/
        g_sem_seats.wait(); 
        g_num_seats     -= mrt->num_seats;  
        mrt->suff_seats  = true; 
        std::cout << GREEN << "[TICKET OFFICE] " << g_num_seats << " tickets left" << RESET << std::endl;
    }else{
        mrt->suff_seats  = false; 
    }
}

/******************************************************
 * Function name:    buyDrinksPopcorn
 * Date created:     23/4/2020
 * Input arguments:  
 * Purpose:          The client sends a request to the sale points to buy drinks and popcorn 
 * 
 ******************************************************/
void buyDrinksPopcorn(int id_client){
    /*Generate the request to buy drinks and popcorn*/
    MsgRequestSalePoint mrsp(id_client, generateRandomNumber(MAX_REQUEST_DRINK_POP), generateRandomNumber(MAX_REQUEST_DRINK_POP));
    g_queue_request_sp.push(&mrsp); 
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    /*Wait turn of sale point*/
    std::unique_lock<std::mutex> ul_turn_food(g_sem_turn_food); 
        g_sem_sale_point.signal();
        int *p_flag_id_sp = &(mrsp.id_sp_attend);
        g_cv_drinks_popcorn.wait(ul_turn_food, [p_flag_id_sp]{return p_flag_id_sp != 0;}); 
        std::cout << CYAN << "[MANAGER] It's the turn of client " << std::to_string(id_client) << " to buy drinks and popcorn" << RESET << std::endl;
        std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] It's my turn for buy drinks and popcorn!" << RESET << std::endl; 
    ul_turn_food.unlock();

    std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] I want " << std::to_string(mrsp.num_drinks) << " drinks and ";
    std::cout << std::to_string(mrsp.num_popcorn) << " popcorn" << RESET << std::endl; 

    std::unique_lock<std::mutex> ul_drink_popcorn(g_sem_drink_popcorn); 
        bool *p_flag_attended = &(mrsp.attended);
        g_cv_receive_food.wait(ul_drink_popcorn, [p_flag_attended]{return *p_flag_attended;}); 
        std::cout << YELLOW << "[CLIENT " << std::to_string(id_client) << "] I have received drinks and popcorn" << RESET << std::endl;     
}

/******************************************************
 * Function name:    salePoint
 * Date created:     23/4/2020
 * Input arguments:  
 * Purpose:          It simulate the sale point. Check if there are enough stocks for the client. 
 *                   If there are, the client is given drinks and popcorn and send a request to pay. If there aren't stocks we call the replenisher
 * 
 ******************************************************/
void salePoint(InfoSalePoint &sp){
    std::cout << MAGENTA << "[SALE POINT " << sp.id << "] Created with " << sp.num_drinks << " drinks and " << sp.num_popcorn << " popcorn" << RESET << std::endl;
    while(true){
        try{ 
            g_sem_sale_point.wait(); 
            g_sem_mutex_access_sp.lock(); 
                MsgRequestSalePoint *mrsp = g_queue_request_sp.front(); 
                g_queue_request_sp.pop(); 
                mrsp->id_sp_attend = sp.id;
                g_cv_drinks_popcorn.notify_all(); 
            g_sem_mutex_access_sp.unlock(); 
            std::this_thread::sleep_for(std::chrono::milliseconds(400)); 

            checkNumDrinksPopcorn(mrsp, std::ref(sp));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::cout << MAGENTA << "[SALE POINT " << sp.id << "] Client " << std::to_string(mrsp->id) << " has been attended" << RESET << std::endl;
            mrsp->attended = true; 
            g_cv_receive_food.notify_all(); 

        }catch(std::exception &e){
            std::cout << MAGENTA << "[SALE POINT " << sp.id << "] An error occurred while attending clients..." << RESET << std::endl;
        }
    }
}

/******************************************************
 * Function name:    checkNumDrinksPopcorn
 * Date created:     22/4/2020
 * Input arguments:  
 * Purpose:          Check number of drinks and popcorn
 * 
 ******************************************************/
void checkNumDrinksPopcorn(MsgRequestSalePoint *mrsp, InfoSalePoint &sp){
   /*Check number of drinks and popcorn*/
            std::cout << MAGENTA << "[SALE POINT " << sp.id << "] The client "<< mrsp->id  <<" has requested " << mrsp->num_drinks << " drinks and " << mrsp->num_popcorn << " popcorn" << RESET << std::endl;
            if((sp.num_popcorn - mrsp->num_drinks <= 0) || (sp.num_popcorn - mrsp->num_popcorn <= 0)){
                requestReplenisher(mrsp, std::ref(sp)); 
                std::cout << MAGENTA << "[SALE POINT " << sp.id << "] " << sp.num_drinks << " drinks and " << sp.num_popcorn << " popcorn left" << RESET << std::endl;
            }else{
                sp.num_drinks  -= mrsp->num_drinks; 
                sp.num_popcorn -= mrsp->num_popcorn;
                std::cout << MAGENTA << "[SALE POINT " << sp.id << "] " << sp.num_drinks << " drinks and " << sp.num_popcorn << " popcorn left" << RESET << std::endl;

                checkPaymentSalePoint(mrsp, std::ref(sp)); 
            }
            g_queue_cinema.push(std::move(g_queue_drinkpop.front())); 
            g_queue_drinkpop.pop();
}

/******************************************************
 * Function name:    requestReplenisher
 * Date created:     28/4/2020
 * Input arguments:  
 * Purpose:          Send a request to replenisher
 * 
 ******************************************************/
void requestReplenisher(MsgRequestSalePoint *mrsp, InfoSalePoint &sp){
    /*Send a request to replenisher*/
    std::cout << MAGENTA << "[SALE POINT " << sp.id << "] The client " << std::to_string(mrsp->id) << " has requested more drinks and popcorn than there are left" << RESET << std::endl;
    std::cout << MAGENTA << "[SALE POINT " << sp.id << "] I need replenish drinks and popcorn" << RESET << std::endl;
    g_queue_request_stock.push(&sp); 
    g_sem_replenisher.signal(); 
          
    sp.num_drinks  -= mrsp->num_drinks; 
    sp.num_popcorn -= mrsp->num_popcorn;
}

/******************************************************
 * Function name:    checkPaymentSalePoint
 * Date created:     28/4/2020
 * Input arguments:  
 * Purpose:          Check if the payment was successful 
 * 
 ******************************************************/
void checkPaymentSalePoint(MsgRequestSalePoint *mrsp, InfoSalePoint &sp){
    g_sem_mutex_payment.lock();  
        MsgRequestPayment mrp(mrsp->id, priorityAssignment(PAY_SP));
        g_queue_request_payment.push(&mrp);
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); /*sleep the thread each time that the client pays drinks and popcorn*/
        std::cout << MAGENTA << "[SALE POINT " << sp.id << "] I request the client's payment" << std::endl; 

        /*Wait confirmation of payment system*/
        std::unique_lock<std::mutex> ul_wait_payment(g_sem_wait_payment); 
            g_sem_payment.signal();  
            bool *p_flag_attended = &(mrp.attended);
            g_cv_payment.wait(ul_wait_payment, [p_flag_attended] {return *p_flag_attended;});
            g_sem_mutex_payment.unlock();  
        ul_wait_payment.unlock();
}

/******************************************************
 * Function name:    replenish
 * Date created:     24/4/2020
 * Input arguments:  
 * Purpose:          It simulate the replenisher. When he receives a request, 
 *                   it replenishes the quantity of drink and popcorn that each sale point has
 * 
 ******************************************************/
void replenish(){
    std::cout << RED << "[REPLENISHER] Created and waiting to receive requests" << RESET << std::endl; 
    while(true){
        try{   
            g_sem_replenisher.wait(); 
            std::this_thread::sleep_for(std::chrono::milliseconds(400)); 
            std::cout << RED << "[REPLENISHER] I have received a request to replenish a sale point" << RESET << std::endl;

            InfoSalePoint *sp = g_queue_request_stock.front(); 
            g_queue_request_stock.pop(); 
            sp->num_drinks  = sp->num_replenish;
            sp->num_popcorn = sp->num_replenish; 

            std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
            std::cout << RED << "[REPLENISHER] I have replenished " << sp->num_drinks << " drinks and " << sp->num_popcorn << " popcorn in sale point " << sp->id << RESET << std::endl;  
        }catch(std::exception &e){
            std::cout << RED << "[REPLENISHER] An error ocurred while replenishing the sale points" << std::endl; 
        }
    }
}

/******************************************************
 * Function name:    paymentSystem 
 * Date created:     13/4/2020
 * Input arguments: 
 * Purpose:          It simulate the pay system
 * 
 ******************************************************/
void paymentSystem(){
    std::cout << BLUE << "[PAYMENT SYSTEM] Payment system open" << RESET << std::endl;  
    while(true){
        try{
            g_sem_payment.wait();
            /*Control the access to payment request queue*/
            g_sem_mutex_access_payment.lock(); 
                MsgRequestPayment *mrp = g_queue_request_payment.top();  
                g_queue_request_payment.pop();
            g_sem_mutex_access_payment.unlock(); 

            switch(mrp->type){
                case 1:
                    std::cout << BLUE << "[PAYMENT SYSTEM] Payment request received. The client " << std::to_string(mrp->id_client) << " has paid tickets" << RESET << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    break; 
                case 2:
                    std::cout << BLUE << "[PAYMENT SYSTEM] Payment request received. The client " << std::to_string(mrp->id_client) << " has paid drinks and popcorn" << RESET << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                    break;
            }
            mrp->attended = true;  
            g_cv_payment.notify_all();
        }catch(std::exception &e){
            std::cout << BLUE << "[PAYMENT SYSTEM] An error occurred while attending clients..." << RESET << std::endl;
        }  
    }
}

/******************************************************
 * Function name:    manager
 * Date created:     13/4/2020
 * Input arguments: 
 * Purpose:          Generate the turns to the clients access to ticket office 
 * 
 ******************************************************/
void manager(){
    std::cout << CYAN << "[MANAGER] Manager is ready" << RESET << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    try{
        for(int i = 1; i <= NUM_CLIENTS; i++){
                std::cout << CYAN << "[MANAGER] It's the turn of client " << std::to_string(i) << " to buy tickets" << RESET << std::endl; 
                g_turn_tickets = i; 
                g_cv_ticket_office.notify_all();  
                g_sem_manager_tickets.lock(); 
        } 
    }catch(std::exception &e){
        std::cout << BOLDCYAN << "[MANAGER] An error occurred while generating turns..." << RESET << std::endl;
    }
}

/******************************************************
 * Function name:    main
 * Date created:     4/4/2020
 * Input arguments: 
 * Purpose:          Principal method of class 
 * 
 ******************************************************/
int main(int argc, char *argv[]){
    if(std::signal(SIGINT, signalHandler) == SIG_ERR){ /*It installs the signal handler*/
        std::cout << BOLDWHITE << "[MAIN] ERROR. The signal CRTL+C hasn't been received correctly \n" << RESET << std::endl; 
    } 

    messageWelcome();
    blockSem(); 
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::thread ticket_office(ticketOffice); 

    InfoSalePoint sp1 = {1, 15, 15, 15};
    std::thread sale_point1(salePoint, std::ref(sp1)); 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    InfoSalePoint sp2 = {2, 12, 12, 12};
    std::thread sale_point2(salePoint, std::ref(sp2)); 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    InfoSalePoint sp3 = {3, 10, 10, 10};
    std::thread sale_point3(salePoint, std::ref(sp3)); 
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::thread payment(paymentSystem); 
    std::thread clients(createClients);
    std::thread thread_manager(manager); 
    std::thread replenisher(replenish);  
 
    payment.join();

    return EXIT_SUCCESS; 
}