#include <iostream>
using namespace std;

class staff
{
public:
    char name[30];
    float basic_salary;
    float HRA;
    float DA;
    float gross_salary;

    void accept();
    void display();
};

void staff::accept()
{
    cout << "\nEnter the name of the staff: ";
    cin >> name;

    cout << "\nEnter the basic salary of the staff: ";
    cin >> basic_salary;
}

void staff::display()
{
    HRA = basic_salary / 74.5;
    DA = basic_salary / 30;
    gross_salary = HRA + DA;

    cout << "\nName: " << name;
    cout << "\nBasic salary: " << basic_salary;
    cout << "\nHRA: " << HRA;
    cout << "\nDA: " << DA;
    cout << "\nGross salary: " << gross_salary;
}

int main()
{
    cout << "Shravani";

    staff s1;
    s1.accept();
    s1.display();

    return 0;
}