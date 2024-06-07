// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#include <cpp/when.h>
#include <debug/harness.h>
#include <memory>

using namespace verona::cpp;

class Account
{
  public:
    int balance;
    Account(int balance) : balance(balance) {}

    uint64_t get_addr() {
      return (uint64_t)this;
    }
};

void test_read_only()
{
  size_t num_accounts = 8;

  std::vector<cown_ptr<Account>> accounts;
  for (size_t i = 0; i < num_accounts; i++)
    accounts.push_back(make_cown<Account>(0));

  cown_ptr<Account> common_account = make_cown<Account>(100);
  when(common_account) <<
    [](acquired_cown<Account> account) { account->balance -= 10; 
    Logging::cout() <<  "first common account " << account->balance << Logging::endl;
    };

  for (size_t i = 0; i < num_accounts; i++)
  {
    when(accounts[i], read(common_account))
      << [i](
           auto write_account,
           auto ro_account) {
           Logging::cout() <<  "write account begin " << i << " balance: " << write_account->balance << Logging::endl;
           Logging::cout() <<  "ro account begin " << ro_account->balance << Logging::endl;
           write_account->balance = ro_account->balance;
           for(int j = 0; j < 10; j++)
            Systematic::yield();
           Logging::cout() <<  "write account end " << i << " balance: " << write_account->balance << Logging::endl;
           Logging::cout() <<  "ro account end " << ro_account->balance << Logging::endl;
         };

    when(read(accounts[i])) << [i](acquired_cown<const Account> account) {
      Logging::cout() <<  "read account begin " << i << " balance: " <<account->balance << Logging::endl;
      for(int j = 0; j < 10; j++)
            Systematic::yield();
      check(account->balance == 90);
      Logging::cout() <<  "read account end " << i << " balance: " << account->balance << Logging::endl;
    };
  }

  when(common_account) <<
    [](acquired_cown<Account> account) { account->balance += 10; 
    Logging::cout() <<  "second common account " << account->balance << Logging::endl;
    };

  when(read(common_account)) << [](acquired_cown<const Account> account) {
    check(account->balance == 100);
    Logging::cout() <<  "third common account " << account->balance << Logging::endl;
  };
}

void test_read_only_nested()
{
  size_t num_accounts = 8;

  std::shared_ptr<std::vector<cown_ptr<Account>>> accounts = std::make_shared<std::vector<cown_ptr<Account>>>();
  for (size_t i = 0; i < num_accounts; i++)
    accounts->push_back(make_cown<Account>(0));

  cown_ptr<Account> common_account = make_cown<Account>(100);
  when(common_account) <<
    [](acquired_cown<Account> account) { account->balance -= 10; 
    Logging::cout() <<  "first common account " << account->balance << Logging::endl;
    };

  for (size_t i = 0; i < num_accounts; i++)
  {
    when(read(common_account))
      << [i, common_account, accounts](acquired_cown<const Account> ro_account) {
          when((*accounts)[i], read(common_account)) << [i](acquired_cown<Account> write_account, acquired_cown<const Account> ro_account) {
          Logging::cout() <<  "write account begin " << i << " balance: " << write_account->balance << Logging::endl;
          Logging::cout() <<  "ro account begin " << ro_account->balance << Logging::endl;
            write_account->balance = ro_account->balance;
          for(int j = 0; j < 10; j++)
            Systematic::yield();
           Logging::cout() <<  "write account end " << i << " balance: " << write_account->balance << Logging::endl;
           Logging::cout() <<  "ro account end " << ro_account->balance << Logging::endl;
          };
        };

    when(read((*accounts)[i])) << [i](acquired_cown<const Account> account) {
      Logging::cout() <<  "read account begin " << i << " balance: " <<account->balance << Logging::endl;
      for(int j = 0; j < 10; j++)
            Systematic::yield();
      //check(account->balance == 90);
      Logging::cout() <<  "read account end " << i << " balance: " << account->balance << Logging::endl;
    };
  }

  when(common_account) <<
    [](acquired_cown<Account> account) { account->balance += 10; 
    Logging::cout() <<  "second common account " << account->balance << Logging::endl;
    };

  when(read(common_account)) << [](acquired_cown<const Account> account) {
    //check(account->balance == 100);
    Logging::cout() <<  "third common account " << account->balance << Logging::endl;
  };
}

void test_read_only_fast_send()
{
  // Test that fast sending to a read cown also reschedules the cown
  cown_ptr<Account> account_one = make_cown<Account>(100);
  cown_ptr<Account> account_two = make_cown<Account>(100);

  when(read(account_one), read(account_two))
    << [](
         acquired_cown<const Account> account_one,
         acquired_cown<const Account> account_two) {
         check(account_one->balance == account_two->balance);
       };

  when(read(account_one), read(account_two))
    << [](
         acquired_cown<const Account> account_one,
         acquired_cown<const Account> account_two) {
         check(account_one->balance == account_two->balance);
       };

  when(account_one, account_two) <<
    [](acquired_cown<Account> account_one, acquired_cown<Account> account_two) {
      check(account_one->balance == account_two->balance);
    };
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);
  harness.run(test_read_only_nested);
}